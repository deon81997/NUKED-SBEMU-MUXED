/*
 * sc55midi.c - SC55MIDI.DRV, a 16-bit Windows MIDI output driver for
 * Windows 98. It shows up as the MIDI device "SC-55 PACK" and hands every
 * byte to SC55W.VXD through its protected-mode API (INT 2Fh AX=1684h by
 * name "SC55W"); SC55W.EXE turns the bytes into sound with the same synth
 * the DOS programs use (SC-55, MT-32 or SoundFont).
 *
 * The VxD is loaded by SC55W.EXE, so the entry point is looked up at each
 * open: start SC55W.EXE (or let it start with Windows) before playing.
 *
 * Built with Open Watcom (16-bit, large model, DLL). Code and data are FIXED
 * because MODM_DATA can be called at interrupt time.
 */
#include <windows.h>
#include <mmsystem.h>

/* MMDDK message numbers */
#define MODM_GETNUMDEVS     1
#define MODM_GETDEVCAPS     2
#define MODM_OPEN           3
#define MODM_CLOSE          4
#define MODM_PREPARE        5
#define MODM_UNPREPARE      6
#define MODM_DATA           7
#define MODM_LONGDATA       8
#define MODM_RESET          9
#define MODM_GETVOLUME      10
#define MODM_SETVOLUME      11

typedef struct {
    HMIDI hMidi;
    DWORD dwCallback;
    DWORD dwInstance;
} MIDIOPENDESC, FAR *LPMIDIOPENDESC;

BOOL WINAPI DriverCallback(DWORD dwCallback, UINT uFlags, HANDLE hDevice, UINT uMessage,
                           DWORD dwUser, DWORD dwParam1, DWORD dwParam2);

#define MAX_CLIENTS 4
typedef struct {
    int used;
    MIDIOPENDESC desc;
    UINT flags;
    BYTE status;            /* running status */
} CLIENT;

static CLIENT clients[MAX_CLIENTS];
static void (FAR *api_ep)(void);
static int opens;

/* ---- VxD protected-mode API ------------------------------------------ */
static const char vxd_name[8] = {'S', 'C', '5', '5', 'W', ' ', ' ', ' '};

static void FAR *get_entry(void);
#pragma aux get_entry = \
    "push es" \
    "push di" \
    "xor bx, bx" \
    "mov ax, seg vxd_name" \
    "mov es, ax" \
    "mov di, offset vxd_name" \
    "mov ax, 1684h" \
    "int 2Fh" \
    "mov dx, es" \
    "mov ax, di" \
    "pop di" \
    "pop es" \
    value [dx ax] modify [bx cx];

static int lookup(void)
{
    void FAR *p;
    if (api_ep) return 1;
    p = get_entry();
    if (p == 0) return 0;
    api_ep = (void (FAR *)(void))p;
    return 1;
}

/* AX=1: send CX bytes (1..3) from DL, DH, BL */
static void send_bytes(unsigned n, BYTE a, BYTE b, BYTE c);
#pragma aux send_bytes = \
    "push bp" \
    "mov ax, 1" \
    "call dword ptr api_ep" \
    "pop bp" \
    parm [cx] [dl] [dh] [bl] modify [ax bx cx dx si di es];

static void send(const BYTE FAR *p, unsigned n)
{
    if (!api_ep) return;
    while (n >= 3) { send_bytes(3, p[0], p[1], p[2]); p += 3; n -= 3; }
    if (n == 2) send_bytes(2, p[0], p[1], 0);
    else if (n == 1) send_bytes(1, p[0], 0, 0);
}

/* ---- MIDI ------------------------------------------------------------ */
static unsigned msg_len(BYTE s)
{
    if (s < 0xF0) return (s & 0xE0) == 0xC0 ? 2 : 3;
    switch (s) {
    case 0xF1: case 0xF3: return 2;
    case 0xF2: return 3;
    default: return 1;
    }
}

static void notify(CLIENT *c, UINT msg, DWORD p1)
{
    DriverCallback(c->desc.dwCallback, c->flags, (HANDLE)c->desc.hMidi, msg, c->desc.dwInstance, p1, 0);
}

static DWORD short_msg(CLIENT *c, DWORD m)
{
    BYTE b[3];
    unsigned n;
    b[0] = (BYTE)m; b[1] = (BYTE)(m >> 8); b[2] = (BYTE)(m >> 16);
    if (b[0] >= 0x80) {
        n = msg_len(b[0]);
        if (b[0] < 0xF0) c->status = b[0];
        else if (b[0] < 0xF8) c->status = 0;
        send(b, n);
    } else if (c->status) {             /* running status: data bytes only */
        n = msg_len(c->status) - 1;
        send(b, n);
    }
    return MMSYSERR_NOERROR;
}

static DWORD long_msg(CLIENT *c, LPMIDIHDR h, DWORD size)
{
    if (!h || !size || !(h->dwFlags & MHDR_PREPARED)) return MIDIERR_UNPREPARED;
    if (h->dwFlags & MHDR_INQUEUE) return MIDIERR_STILLPLAYING;
    send((const BYTE FAR *)h->lpData, (unsigned)h->dwBufferLength);
    c->status = 0;
    h->dwFlags |= MHDR_DONE;
    notify(c, MOM_DONE, (DWORD)h);
    return MMSYSERR_NOERROR;
}

static void all_off(void)
{
    BYTE m[3];
    int ch;
    for (ch = 0; ch < 16; ch++) {
        m[0] = (BYTE)(0xB0 | ch);
        m[1] = 0x40; m[2] = 0; send(m, 3);      /* sustain off       */
        m[1] = 0x7B; send(m, 3);                /* all notes off     */
        m[1] = 0x78; send(m, 3);                /* all sound off     */
    }
}

DWORD FAR PASCAL __export __loadds modMessage(UINT dev, UINT msg, DWORD user, DWORD p1, DWORD p2)
{
    CLIENT *c;
    int i;
    if (dev != 0 && msg != MODM_GETNUMDEVS) return MMSYSERR_BADDEVICEID;
    switch (msg) {
    case MODM_GETNUMDEVS:
        return 1;
    case MODM_GETDEVCAPS: {
        MIDIOUTCAPS FAR *caps = (MIDIOUTCAPS FAR *)p1;
        MIDIOUTCAPS mc;
        static const char name[] = "SC-55 PACK";
        BYTE FAR *d;
        const BYTE *s;
        unsigned k, n = (unsigned)p2;
        for (k = 0; k < sizeof mc; k++) ((BYTE *)&mc)[k] = 0;
        mc.wMid = 1;                    /* MM_MICROSOFT */
        mc.wPid = 1;
        mc.vDriverVersion = 0x0100;
        for (k = 0; name[k]; k++) mc.szPname[k] = name[k];
        mc.wTechnology = MOD_MIDIPORT;
        mc.wVoices = 0;
        mc.wNotes = 0;
        mc.wChannelMask = 0xFFFF;
        mc.dwSupport = 0;
        if (n > sizeof mc) n = sizeof mc;
        d = (BYTE FAR *)caps; s = (const BYTE *)&mc;
        for (k = 0; k < n; k++) d[k] = s[k];
        return MMSYSERR_NOERROR;
    }
    case MODM_OPEN: {
        LPMIDIOPENDESC od = (LPMIDIOPENDESC)p1;
        /* SC55W.EXE must be running: it loads the VxD and makes the sound */
        if (!FindWindow("SC55W_Main", NULL) || !lookup()) return MMSYSERR_NOTENABLED;
        for (i = 0; i < MAX_CLIENTS && clients[i].used; i++) ;
        if (i == MAX_CLIENTS) return MMSYSERR_ALLOCATED;
        c = &clients[i];
        c->used = 1;
        c->desc = *od;
        c->flags = HIWORD(p2);
        c->status = 0;
        *(DWORD FAR *)user = (DWORD)i;
        opens++;
        notify(c, MOM_OPEN, 0);
        return MMSYSERR_NOERROR;
    }
    }
    if ((DWORD)user >= MAX_CLIENTS || !clients[(int)user].used) return MMSYSERR_INVALHANDLE;
    c = &clients[(int)user];
    switch (msg) {
    case MODM_CLOSE:
        notify(c, MOM_CLOSE, 0);
        c->used = 0;
        if (--opens == 0) api_ep = 0;       /* look the VxD up again next time */
        return MMSYSERR_NOERROR;
    case MODM_DATA:
        return short_msg(c, p1);
    case MODM_LONGDATA:
        return long_msg(c, (LPMIDIHDR)p1, p2);
    case MODM_RESET:
        all_off();
        c->status = 0;
        return MMSYSERR_NOERROR;
    case MODM_PREPARE:
    case MODM_UNPREPARE:
        return MMSYSERR_NOTSUPPORTED;       /* MMSYSTEM does it */
    }
    return MMSYSERR_NOTSUPPORTED;
}

LRESULT FAR PASCAL __export __loadds DriverProc(DWORD id, HDRVR h, UINT msg, LPARAM p1, LPARAM p2)
{
    switch (msg) {
    case DRV_LOAD:
    case DRV_FREE:
    case DRV_ENABLE:
    case DRV_DISABLE:
    case DRV_CLOSE:
        return 1;
    case DRV_OPEN:
        return 1;
    case DRV_QUERYCONFIGURE:
        return 0;
    case DRV_INSTALL:
    case DRV_REMOVE:
        return DRVCNF_OK;
    }
    return DefDriverProc(id, h, msg, p1, p2);
}

int FAR PASCAL LibMain(HINSTANCE inst, WORD ds, WORD heap, LPSTR cmd)
{
    (void)inst; (void)ds; (void)heap; (void)cmd;
    return 1;
}
