/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* USB keyboard and mouse for DOS programs, through the PC's own keyboard controller (8042).
 *
 * Keyboard: each USB boot report is compared with the last one; pressed / released keys become set 1 scan
 * codes (make / break, E0 prefix for the extended keys) and are put into the controller's output buffer with
 * command D2h ("write keyboard output buffer"): the controller raises IRQ 1 and the BIOS / the game reads port
 * 60h as if a PS/2 keyboard had sent them. USB keyboards do not repeat keys themselves, so held keys repeat
 * here (after 0.5 s, 30 per second). The lock key LEDs follow the BIOS's flags (0040:0017).
 *
 * Mouse (when ports 60h / 64h are trapped): commands a mouse driver sends to the PS/2 mouse (64h <- D4h, then
 * 60h <- command) never reach the controller; they are answered here like a PS/2 mouse would (ACK FAh, reset
 * AAh 00h, ID 00h - or 03h after the IntelliMouse sample-rate knock 200, 100, 80 - status, ...), and once the
 * driver has enabled streaming (F4h) USB movement is sent as 3-byte (4 with the wheel) packets with command
 * D3h ("write mouse output buffer": IRQ 12). So any DOS PS/2 mouse driver (CTMOUSE, MOUSE.COM) loaded after
 * SBEMU finds a PS/2 mouse. Everything else on the two ports goes to the controller unchanged.
 *
 * One byte at a time (from the sound interrupt), only when the controller is idle (no byte waiting, input
 * buffer empty), so a real key press or a byte some program waits for is never overwritten. Keyboard bytes are held back for 20 ms after a program wrote to the controller itself
 * (keyboard LED or other commands, whose answers it is waiting for). */
#include <string.h>
#include "vkbc.h"
#include "usbpad.h"
#include "untrapio.h"
#include "dpmi/dpmi.h"

static uint64_t Cps;
static inline uint64_t tsc(void) { uint32_t lo, hi; __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi)); return ((uint64_t)hi << 32) | lo; }
static uint64_t ms2c(uint32_t ms) { return Cps * ms / 1000ULL; }

/* ---------------------------------------------------------------- byte queues */
#define KQN 128
#define AQN 32
static volatile uint8_t Kq[KQN]; static volatile int Kh, Kt;
static volatile uint8_t Aq[AQN]; static volatile int Ah, At;
static int LastAux;
static uint64_t Hold;

static void kpush(uint8_t b)
{
    CLIS();
    { int n = (Kh + 1) % KQN; if (n != Kt) { Kq[Kh] = b; Kh = n; } }
    STIL();
}
static void apush(uint8_t b)
{
    int n = (Ah + 1) % AQN;
    if (n != At) { Aq[Ah] = b; Ah = n; }
}

/* A byte put in with D2h/D3h is "in flight" until it has shown up in the output buffer and been read.
 * Real keyboard controllers (the D810's embedded controller) take a while to move it there: status
 * then reads idle (bits 0 and 1 clear) although the byte has not arrived yet, and a next D3h in that
 * gap loses or overwrites it - the driver's 3-byte packets slip and it sees random moves and clicks.
 * Emulators are instant, so they never showed it. */
static int Flight, FlightSeen;
static uint64_t FlightT;

static void kbc_put(int aux, uint8_t b)
{
    int n;
    UntrappedIO_OUT(0x64, aux ? 0xD3 : 0xD2);
    for (n = 0; n < 2000 && (UntrappedIO_IN(0x64) & 2); ++n) {}
    UntrappedIO_OUT(0x60, b);
    Flight = 1; FlightSeen = 0; FlightT = tsc();
}

/* 1 = the last byte put in is still on its way or unread */
static int in_flight(uint8_t st, uint64_t t)
{
    if (!Flight) return 0;
    if (st & 1) FlightSeen = 1;                                  /* arrived: wait until it is read */
    else if (FlightSeen || t - FlightT > ms2c(4)) Flight = 0;    /* read (or long enough to have been) */
    return Flight;
}

/* ---------------------------------------------------------------- PS/2 mouse */
static int MsOn, MsSeen, AuxNext, MsArg, MsStream, MsRemote, MsWrap, MsRate = 100, MsRes = 2, MsScale, MsId;
static uint8_t MsRates[3];
static volatile int MDx, MDy, MDw, MB, MDirty;
static uint64_t MsLast;
/* BIOS mode (beta.76, no real-mode traps): the INT 15h stub's data bytes tell whether the mouse driver has
 * enabled the BIOS mouse (C200h) and its resolution; USB movement is then handed to the driver's own packet
 * handler as whole packets (bios_deliver, beta.78), beside the touchpad's, which the BIOS hands over. */
static uint32_t BiosLin;

static void ms_defaults(void) { MsRate = 100; MsRes = 2; MsScale = 0; MsStream = 0; MsRemote = 0; }

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* movement is collected in 1/8 counts: USB counts x 2^resolution / 8 (resolution 3 = 8 counts/mm: 1:1) */
static void ms_take(uint8_t p[4])
{
    int dx = clamp(MDx / 8, -255, 255), dy = clamp(-MDy / 8, -255, 255), dz = clamp(-MDw, -8, 7);
    MDx -= dx * 8; MDy += dy * 8; MDw += dz;
    p[0] = (uint8_t)(0x08 | (MB & 7) | (dx < 0 ? 0x10 : 0) | (dy < 0 ? 0x20 : 0));
    p[1] = (uint8_t)dx;
    p[2] = (uint8_t)dy;
    p[3] = (uint8_t)dz;
    MDirty = (MDx / 8) || (MDy / 8) || MDw;
}

static void ms_packet(void)
{
    uint8_t p[4];
    ms_take(p);
    apush(p[0]); apush(p[1]); apush(p[2]);
    if (MsId == 3) apush(p[3]);
}

/* BIOS mode: the whole packet to the mouse driver's handler through the INT 15h stub (see main.c MAIN_I15Stub):
 * the stub's deliver (offset 24h) calls the handler the driver gave the BIOS (C207h) with status, X, Y, 0 on the
 * stack, as the BIOS's IRQ 12 handler does. Putting the 3 bytes into the controller one by one (D3h) did not
 * work where the BIOS reads a whole packet in one IRQ 12 and gives up when the next byte is late (Award; the
 * D810's): nothing moved, or the packets slipped - random moves and clicks. */
static int bios_deliver(void)
{
    DPMI_REG r;
    uint8_t p[4];
    int sx = MDx, sy = MDy;
    int sw = MDw, sd = MDirty;
    if (DPMI_LoadB(BiosLin + 15)) return 0;                     /* the driver's handler is running: later */
    ms_take(p);
    DPMI_StoreW(BiosLin + 20, p[0]); DPMI_StoreW(BiosLin + 22, p[1]); DPMI_StoreW(BiosLin + 24, p[2]);
    memset(&r, 0, sizeof(r));
    r.w.cs = (uint16_t)(BiosLin >> 4);
    r.w.ip = 0x24;
    DPMI_CallRealModeRETF(&r);
    if (r.w.ax != 1) { MDx = sx; MDy = sy; MDw = sw; MDirty = sd; return 0; }   /* not taken: keep it */
    return 1;
}

static void ms_cmd(uint8_t b)
{
    MsSeen = 1;
    if (MsWrap && b != 0xEC && b != 0xFF) { apush(b); return; }     /* wrap mode: echo */
    if (MsArg)
    {
        int c = MsArg;
        MsArg = 0;
        apush(0xFA);
        if (c == 0xF3)
        {
            MsRate = b < 10 ? 10 : b > 200 ? 200 : b;
            MsRates[0] = MsRates[1]; MsRates[1] = MsRates[2]; MsRates[2] = b;
            if (MsRates[0] == 200 && MsRates[1] == 100 && MsRates[2] == 80) MsId = 3;   /* IntelliMouse: wheel */
        }
        else if (c == 0xE8) MsRes = b & 3;
        return;
    }
    switch (b)
    {
    case 0xFF: Ah = At; ms_defaults(); MsId = 0; MsWrap = 0; apush(0xFA); apush(0xAA); apush(0x00); break;
    case 0xF6: ms_defaults(); apush(0xFA); break;
    case 0xF5: MsStream = 0; apush(0xFA); break;
    case 0xF4: MsStream = 1; MDx = MDy = MDw = 0; MDirty = 0; apush(0xFA); break;
    case 0xF3: case 0xE8: MsArg = b; apush(0xFA); break;
    case 0xF2: apush(0xFA); apush((uint8_t)MsId); break;
    case 0xF0: MsRemote = 1; apush(0xFA); break;
    case 0xEA: MsRemote = 0; apush(0xFA); break;
    case 0xEB: apush(0xFA); ms_packet(); break;
    case 0xE9:
        apush(0xFA);
        apush((uint8_t)((MsRemote << 6) | (MsStream << 5) | (MsScale << 4) | ((MB & 1) << 2) | ((MB & 4) >> 1) | ((MB & 2) >> 1)));
        apush((uint8_t)MsRes); apush((uint8_t)MsRate);
        break;
    case 0xEE: MsWrap = 1; apush(0xFA); break;
    case 0xEC: MsWrap = 0; apush(0xFA); break;
    case 0xE6: MsScale = 0; apush(0xFA); break;
    case 0xE7: MsScale = 1; apush(0xFA); break;
    default:   apush(0xFA); break;                                  /* anything else: acknowledged */
    }
}

/* beta.80: a keyboard byte left unread in the controller for 50 ms got no interrupt (seen in 86Box with pad keys
 * repeating: a byte put in while the BIOS's IRQ 1 handler had the keyboard switched off, AD..AE). Nothing reads it, so
 * nothing more could go in - every key after it was stuck. It is taken back and put in again (fresh IRQ 1), in front
 * of the queue: nothing is lost. */
static uint64_t ObfSince;
static uint32_t Unstuck;
uint32_t VKBC_Unstuck(void) { return Unstuck; }

/* one byte into the controller if it is idle; mouse answers first */
static void inject(void)
{
    uint8_t st;
    uint64_t t;
    CLIS();
    t = tsc();
    if (BiosLin && MDirty && t - MsLast >= Cps / 100)
    {
        const uint8_t on = DPMI_LoadB(BiosLin + 9), ps = DPMI_LoadB(BiosLin + 10);
        if ((on & 1) && (ps == 0 || ps == 3) && DPMI_LoadW(BiosLin + 18))
        {
            const int r = DPMI_LoadB(BiosLin + 11) & 3;
            if (r != MsRes) { MDx = MDx >> MsRes << r; MDy = MDy >> MsRes << r; MsRes = r; }
            MsId = 0;
            if (bios_deliver()) MsLast = t;
        }
        else { MDx = MDy = MDw = 0; MDirty = 0; }                      /* no driver listening: drop it */
    }
    st = (uint8_t)UntrappedIO_IN(0x64);
    if ((st & 0x21) == 0x01)                                        /* keyboard byte waiting */
    {
        if (!ObfSince) ObfSince = t;
        else if (t - ObfSince > ms2c(50))
        {
            uint8_t b = (uint8_t)UntrappedIO_IN(0x60);
            int n = (Kt + KQN - 1) % KQN;
            if (n != Kh) { Kt = n; Kq[Kt] = b; }
            ObfSince = 0; Flight = 0; ++Unstuck;
            st = (uint8_t)UntrappedIO_IN(0x64);
        }
    }
    else ObfSince = 0;
    if (!in_flight(st, t) && (st & 3) == 0)
    {
        int ha, hk;
        if (Ah == At && MsOn && MsStream && !MsRemote && MDirty && t - MsLast >= Cps / (uint64_t)MsRate)
        { MsLast = t; ms_packet(); }
        ha = Ah != At;
        hk = Kh != Kt && t >= Hold;
        if (ha && (!hk || !LastAux)) { kbc_put(1, Aq[At]); At = (At + 1) % AQN; LastAux = 1; }
        else if (hk) { kbc_put(0, Kq[Kt]); Kt = (Kt + 1) % KQN; LastAux = 0; }
    }
    STIL();
}

/* ---------------------------------------------------------------- keyboard */
/* HID usage (keyboard page) -> set 1 scan code, E0xx = extended, 0 = none */
static const uint16_t Usage[0x66] =
{
    0, 0, 0, 0, 0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26,             /* 00 - 0F  a..l */
    0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C, 0x02, 0x03, /* 10 - 1F  m..z 1 2 */
    0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x1C, 0x01, 0x0E, 0x0F, 0x39, 0x0C, 0x0D, 0x1A, /* 20 - 2F  3..0 enter esc bksp tab space - = [ */
    0x1B, 0x2B, 0x2B, 0x27, 0x28, 0x29, 0x33, 0x34, 0x35, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, /* 30 - 3F  ] \ # ; ' ` , . / caps F1..F6 */
    0x41, 0x42, 0x43, 0x44, 0x57, 0x58, 0xE037, 0x46, 0, 0xE052, 0xE047, 0xE049, 0xE053, 0xE04F, 0xE051, 0xE04D, /* 40 - 4F F7..F12 prtsc scroll pause ins home pgup del end pgdn right */
    0xE04B, 0xE050, 0xE048, 0x45, 0xE035, 0x37, 0x4A, 0x4E, 0xE01C, 0x4F, 0x50, 0x51, 0x4B, 0x4C, 0x4D, 0x47, /* 50 - 5F left down up numlock kp/ * - + enter 1..7 */
    0x48, 0x49, 0x52, 0x53, 0x56, 0xE05D,                                                            /* 60 - 65 kp8 kp9 kp0 kp. \| app */
};
static const uint16_t Mods[8] = { 0x1D, 0x2A, 0x38, 0xE05B, 0xE01D, 0x36, 0xE038, 0xE05C };
static uint8_t  KPrev[8][8];
static int      Rep, RepId;
static uint64_t RepT;
/* beta.80: how many sources hold each key (USB keyboards, /JOYKEY, the pad layout): a key the pad and a USB keyboard
 * both hold is let go only when the last one lets go (the PS/2 keyboard is not seen here: its bytes don't pass SBEMU) */
static uint8_t  Held[2][128];
/* pad keys repeat like a real keyboard's: the last one pressed, after 0.5 s, 30 per second (lock keys don't) */
static uint16_t PadRep;
static uint64_t PadRepT;

static void put_key(uint16_t code, int down)    /* the bytes only */
{
    if (code > 0xFF) kpush(0xE0);
    kpush((uint8_t)((code & 0x7F) | (down ? 0 : 0x80)));
}

void VKBC_Key(uint16_t code, int down)
{
    uint8_t* h = &Held[code > 0xFF][code & 0x7F];
    if (down) { if (*h < 255) ++*h; }
    else if (*h > 1) { --*h; return; }              /* still held by another source */
    else *h = 0;
    put_key(code, down);
}

/* pad layout keys (padmap.c emit: one key's bytes, or the Pause sequence) */
void VKBC_PadBytes(const uint8_t* b, int n)
{
    uint16_t code;
    int down;
    if (n <= 0) return;
    if (b[0] == 0xE1 || n > 2 || (n == 2 && b[0] != 0xE0)) { VKBC_Bytes(b, n); return; }
    code = n == 2 ? (uint16_t)(0xE000 | (b[1] & 0x7F)) : (uint16_t)(b[0] & 0x7F);
    down = !((n == 2 ? b[1] : b[0]) & 0x80);
    VKBC_Key(code, down);
    if (down && code != 0x3A && code != 0x45 && code != 0x46) { PadRep = code; PadRepT = tsc() + ms2c(500); }
    else if (!down && code == PadRep) PadRep = 0;
}

void VKBC_Bytes(const uint8_t* b, int n)
{
    int i;
    for (i = 0; i < n; ++i) kpush(b[i]);
}

static void usage_key(int u, int down, int again)   /* again: a repeat of a held key (not counted) */
{
    if (u == 0x46)                                                      /* Print Screen */
    {
        if (down) { kpush(0xE0); kpush(0x2A); kpush(0xE0); kpush(0x37); }
        else { kpush(0xE0); kpush(0xB7); kpush(0xE0); kpush(0xAA); }
        return;
    }
    if (u == 0x48)                                                      /* Pause: make only */
    {
        static const uint8_t p[6] = { 0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5 };
        int i;
        if (down) for (i = 0; i < 6; ++i) kpush(p[i]);
        return;
    }
    if (u < 0x66 && Usage[u]) { if (again) put_key(Usage[u], 1); else VKBC_Key(Usage[u], down); }
}

static int has(const uint8_t* r, int u) { int i; for (i = 2; i < 8; ++i) if (r[i] == u) return 1; return 0; }

void VKBC_UsbKeys(int id, const uint8_t* r)
{
    uint8_t* p = KPrev[id & 7];
    int i;
    for (i = 0; i < 8; ++i)
        if ((r[0] ^ p[0]) & (1 << i)) VKBC_Key(Mods[i], (r[0] >> i) & 1);
    for (i = 2; i < 8; ++i)
        if (p[i] > 3 && !has(r, p[i])) { usage_key(p[i], 0, 0); if (Rep == p[i] && RepId == id) Rep = 0; }
    for (i = 2; i < 8; ++i)
        if (r[i] > 3 && !has(p, r[i]))
        {
            usage_key(r[i], 1, 0);
            if (r[i] != 0x48 && r[i] != 0x39 && r[i] != 0x47 && r[i] != 0x53) { Rep = r[i]; RepId = id; RepT = tsc() + ms2c(500); }
        }
    memcpy(p, r, 8);
}

void VKBC_UsbMouse(int b, int dx, int dy, int dw)
{
    if (!MsOn && !BiosLin) return;
    CLIS();
    MDx += dx * (1 << MsRes); MDy += dy * (1 << MsRes); MDw += dw;
    if (b != MB || dx || dy || dw) MDirty = 1;
    MB = b;
    STIL();
}

int VKBC_Leds(void)
{
    uint8_t f = DPMI_LoadB(0x417);
    return ((f & 0x20) ? 1 : 0) | ((f & 0x40) ? 2 : 0) | ((f & 0x10) ? 4 : 0);
}

int VKBC_MouseSeen(void) { return MsSeen || (BiosLin && (DPMI_LoadB(BiosLin + 9) & 1)); }

void VKBC_SetBios(uint32_t lin) { BiosLin = lin; MsRes = 2; }

void VKBC_Tick(void)
{
    uint64_t t = tsc();
    if (Rep && t >= RepT && Kh == Kt) { usage_key(Rep, 1, 1); RepT = t + ms2c(33); }
    if (PadRep && t >= PadRepT && Kh == Kt) { put_key(PadRep, 1); PadRepT = t + ms2c(33); }
    USBPAD_SetLeds(VKBC_Leds());
    inject();
}

uint32_t VKBC_IOHandler(uint32_t port, uint32_t val, uint32_t out)
{
    if (port == 0x64)
    {
        /* a BIOS IRQ 12 handler that reads the whole packet in one go (Award) polls here for the next
         * byte with interrupts off: the sound interrupt cannot put it in then, so it goes in right now */
        if (!out) { if (Ah != At) inject(); return UntrappedIO_IN(0x64); }
        if (MsOn && val == 0xD4) { AuxNext = 1; return val; }       /* next byte on 60h is for the mouse: ours */
        AuxNext = 0;
        Hold = tsc() + ms2c(20);
        UntrappedIO_OUT(0x64, (uint8_t)val);
        return val;
    }
    if (out)
    {
        if (AuxNext)
        {
            CLIS();
            AuxNext = 0;
            ms_cmd((uint8_t)val);
            STIL();
            inject();
            return val;
        }
        Hold = tsc() + ms2c(20);
        UntrappedIO_OUT(0x60, (uint8_t)val);
        return val;
    }
    {
        /* no next byte from here: QPIEMU reads the port once more after this handler, which would
         * swallow it (seen in QEMU: lost keys) - the sound interrupt puts it in */
        return UntrappedIO_IN(0x60);
    }
}

void VKBC_Init(uint64_t cps, int mouse)
{
    Cps = cps ? cps : 1000000000ULL;
    Kh = Kt = Ah = At = 0;
    memset(KPrev, 0, sizeof(KPrev));
    memset(Held, 0, sizeof(Held));
    Rep = 0; PadRep = 0; ObfSince = 0; Unstuck = 0;
    MsOn = mouse; MsSeen = 0; BiosLin = 0; AuxNext = 0; MsArg = 0; MsWrap = 0; MsId = 0;
    ms_defaults();
    MDx = MDy = MDw = MB = MDirty = 0;
}
