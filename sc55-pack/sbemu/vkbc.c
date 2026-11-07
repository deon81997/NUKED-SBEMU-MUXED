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

static void kbc_put(int aux, uint8_t b)
{
    int n;
    UntrappedIO_OUT(0x64, aux ? 0xD3 : 0xD2);
    for (n = 0; n < 2000 && (UntrappedIO_IN(0x64) & 2); ++n) {}
    UntrappedIO_OUT(0x60, b);
}

/* ---------------------------------------------------------------- PS/2 mouse */
static int MsOn, MsSeen, AuxNext, MsArg, MsStream, MsRemote, MsWrap, MsRate = 100, MsRes = 2, MsScale, MsId;
static uint8_t MsRates[3];
static volatile int MDx, MDy, MDw, MB, MDirty;
static uint64_t MsLast;

static void ms_defaults(void) { MsRate = 100; MsRes = 2; MsScale = 0; MsStream = 0; MsRemote = 0; }

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* movement is collected in 1/8 counts: USB counts x 2^resolution / 8 (resolution 3 = 8 counts/mm: 1:1) */
static void ms_packet(void)
{
    int dx = clamp(MDx / 8, -255, 255), dy = clamp(-MDy / 8, -255, 255), dz = clamp(-MDw, -8, 7);
    MDx -= dx * 8; MDy += dy * 8; MDw += dz;
    apush((uint8_t)(0x08 | (MB & 7) | (dx < 0 ? 0x10 : 0) | (dy < 0 ? 0x20 : 0)));
    apush((uint8_t)dx);
    apush((uint8_t)dy);
    if (MsId == 3) apush((uint8_t)dz);
    MDirty = (MDx / 8) || (MDy / 8) || MDw;
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

/* one byte into the controller if it is idle; mouse answers first */
static void inject(void)
{
    CLIS();
    if ((UntrappedIO_IN(0x64) & 3) == 0)
    {
        uint64_t t = tsc();
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

void VKBC_Key(uint16_t code, int down)
{
    if (code > 0xFF) kpush(0xE0);
    kpush((uint8_t)((code & 0x7F) | (down ? 0 : 0x80)));
}

static void usage_key(int u, int down)
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
    if (u < 0x66 && Usage[u]) VKBC_Key(Usage[u], down);
}

static int has(const uint8_t* r, int u) { int i; for (i = 2; i < 8; ++i) if (r[i] == u) return 1; return 0; }

void VKBC_UsbKeys(int id, const uint8_t* r)
{
    uint8_t* p = KPrev[id & 7];
    int i;
    for (i = 0; i < 8; ++i)
        if ((r[0] ^ p[0]) & (1 << i)) VKBC_Key(Mods[i], (r[0] >> i) & 1);
    for (i = 2; i < 8; ++i)
        if (p[i] > 3 && !has(r, p[i])) { usage_key(p[i], 0); if (Rep == p[i] && RepId == id) Rep = 0; }
    for (i = 2; i < 8; ++i)
        if (r[i] > 3 && !has(p, r[i]))
        {
            usage_key(r[i], 1);
            if (r[i] != 0x48 && r[i] != 0x39 && r[i] != 0x47 && r[i] != 0x53) { Rep = r[i]; RepId = id; RepT = tsc() + ms2c(500); }
        }
    memcpy(p, r, 8);
}

void VKBC_UsbMouse(int b, int dx, int dy, int dw)
{
    if (!MsOn) return;
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

int VKBC_MouseSeen(void) { return MsSeen; }

void VKBC_Tick(void)
{
    uint64_t t = tsc();
    if (Rep && t >= RepT && Kh == Kt) { usage_key(Rep, 1); RepT = t + ms2c(33); }
    USBPAD_SetLeds(VKBC_Leds());
    inject();
}

uint32_t VKBC_IOHandler(uint32_t port, uint32_t val, uint32_t out)
{
    if (port == 0x64)
    {
        if (!out) return UntrappedIO_IN(0x64);
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
    Rep = 0;
    MsOn = mouse; MsSeen = 0; AuxNext = 0; MsArg = 0; MsWrap = 0; MsId = 0;
    ms_defaults();
    MDx = MDy = MDw = MB = MDirty = 0;
}
