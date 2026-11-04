#include "vdiag.h"
#include "untrapio.h"
#include "dpmi/dpmi.h"
#include <string.h>
#include <time.h>

volatile int DIAG_Armed;
/* timed measurement (GAMETEST): 0xA9 n = stop by itself after n seconds and keep that report */
static int D_WantSecs, D_LimitSecs, D_Frozen;
static void snapshot(void);

static double   D_Cps;                /* CPU cycles per second (only used outside interrupts) */
static uint64_t D_CpsI, D_CpUs;       /* integer copies: no FPU use inside interrupts (the game owns the FPU) */
static uint64_t D_ArmTsc;

/* sound card interrupt */
static uint64_t D_IsrT0, D_IsrLastEnd, D_StretchStart;
static uint32_t D_IsrCount, D_IsrMaxUs, D_IsrHist[4], D_StretchMaxUs, D_FramesMax;
static uint64_t D_IsrTotal;
static uint64_t D_LostMilli;           /* expected lost game-timer ticks x1000 */
static uint64_t D_StretchCount;

/* game timer (PIT channel 0) */
static uint32_t D_PitMax, D_PitWinMax, D_PitSamples, D_PitDiv, D_PitFastest;
static uint64_t D_ExpectMilli, D_LastBegin; /* game-timer ticks that should have happened x1000 */

/* game's SB handler */
static uint64_t D_VirqT0, D_VirqTotal;
static uint32_t D_VirqCount, D_VirqMaxUs;

/* synth */
static uint64_t D_RenderTotal; static uint32_t D_RenderCount, D_RenderMaxUs, D_Frames, D_Freq, D_Synth;
static uint32_t D_GovLevel, D_Fx, D_Voices;

/* traps / midi */
static uint32_t D_Traps[6], D_MidiBytes, D_MidiDropped;
static uint32_t D_Replayed;
static uint64_t D_BiosExp0, D_BiosGot0, D_BiosExp, D_BiosGot; static int D_BiosInit;
static int32_t D_FifoMin = -1; static uint32_t D_UnderBase, D_UnderNow; static int D_UnderInit;

static uint8_t  D_Buf[256];
static int      D_BufLen, D_BufPos;

uint64_t DIAG_Tsc(void)
{
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

void DIAG_Calibrate(void)
{
    uclock_t u0 = uclock(), u1;
    uint64_t c0 = DIAG_Tsc(), c1;
    do { u1 = uclock(); } while ((u1 - u0) < UCLOCKS_PER_SEC / 10);
    c1 = DIAG_Tsc();
    D_Cps = (double)(c1 - c0) * UCLOCKS_PER_SEC / (double)(u1 - u0);
    D_CpsI = (uint64_t)D_Cps;
    D_CpUs = D_CpsI / 1000000; if (!D_CpUs) D_CpUs = 1;
}

static uint32_t us(uint64_t cycles) { return D_CpsI ? (uint32_t)(cycles / D_CpUs) : 0; }
static uint64_t period_cycles(uint32_t div) { return (uint64_t)div * D_CpsI / 1193182u; }

static void reset(void)
{
    D_IsrT0 = D_IsrLastEnd = D_StretchStart = 0;
    D_IsrCount = D_IsrMaxUs = D_StretchMaxUs = D_FramesMax = 0; memset(D_IsrHist, 0, sizeof(D_IsrHist));
    D_IsrTotal = D_LostMilli = D_StretchCount = 0;
    D_PitMax = D_PitWinMax = D_PitSamples = 0; D_PitDiv = 0; D_PitFastest = 0; D_ExpectMilli = 0; D_LastBegin = 0;
    D_VirqTotal = 0; D_VirqCount = D_VirqMaxUs = 0;
    D_RenderTotal = 0; D_RenderCount = D_RenderMaxUs = D_Frames = 0;
    memset(D_Traps, 0, sizeof(D_Traps)); D_MidiBytes = D_MidiDropped = 0;
    D_FifoMin = -1; D_UnderInit = 0; D_Replayed = 0; D_BiosInit = 0; D_BiosExp = D_BiosGot = 0;
    D_ArmTsc = DIAG_Tsc();
}

/* a stretch = sound interrupts that follow each other with (almost) no gap: the game's timer
 * cannot tick during the whole stretch */
static void close_stretch(void)
{
    uint64_t len;
    if (!D_StretchStart || D_IsrLastEnd <= D_StretchStart) return;
    len = D_IsrLastEnd - D_StretchStart;
    if (us(len) > D_StretchMaxUs) D_StretchMaxUs = us(len);
    ++D_StretchCount;
    if (D_PitDiv && D_PitDiv < 65000 && D_CpsI) /* 18.2 Hz = DOS prompt, not a game */
    {
        uint64_t period = period_cycles(D_PitDiv);   /* game timer period in CPU cycles */
        uint64_t milli = period ? len * 1000u / period : 0;
        if (milli > 1000) D_LostMilli += milli - 1000;
    }
}

static void sample_pit(void)
{
    uint8_t lo, hi; uint32_t v;
    UntrappedIO_OUT(0x43, 0x00);           /* latch counter 0 */
    lo = UntrappedIO_IN(0x40); hi = UntrappedIO_IN(0x40);
    v = lo | ((uint32_t)hi << 8); if (!v) v = 65536;
    if (v > D_PitWinMax) D_PitWinMax = v;
    if (v > D_PitMax) D_PitMax = v;
    if ((++D_PitSamples & 31) == 0) { D_PitDiv = D_PitWinMax; D_PitWinMax = 0; } /* follows rate changes */
    else if (!D_PitDiv && D_PitSamples >= 8) D_PitDiv = D_PitWinMax;
    if (D_PitDiv && D_PitDiv < 65000 && (!D_PitFastest || D_PitDiv < D_PitFastest)) D_PitFastest = D_PitDiv;
}

/* ---------------------------------------------------------------- key player (GAMETEST automation)
 * GAMETEST sends a short key script (MPU command 0xAB, then the script bytes, ending with 0x00).
 * SBEMU "types" it into the game from the sound interrupt with keyboard-controller command D2h
 * ("write keyboard output buffer"): the controller then raises IRQ 1 exactly as for a real key, so it
 * works with games that have their own keyboard handler (Duke 3D, Doom) as well as with the BIOS.
 * Script bytes:  01..7F tap that key (set-1 scan code: press, release)   83 c  tap extended key (E0 c)
 *                80 n   wait n tenths of a second                        81    wait until the timed
 *                00     end of script                                          measurement (0xA9) is over
 * 0xAC clears the script at once (GAMETEST sends it as soon as the game has exited).
 * Only one byte is written per interrupt, and only when the controller's buffers are empty, so a real
 * key press or touchpad byte is never overwritten; if nothing reads the keyboard for 3 s the script stops. */
#define K_MAX 96
static uint8_t  K_Scr[K_MAX]; static int K_Len, K_Pos, K_Rx, K_Run;
static uint8_t  K_Out[4]; static int K_OutN, K_OutPos;  /* bytes of the current key event */
static int      K_Phase;                                /* 0 next script item, 1 sending press, 2 sending release */
static uint64_t K_WaitUntil, K_Stuck;
volatile int    DIAG_KeyEnd;                            /* the timed measurement is over */

static int kbc_free(void) { return (UntrappedIO_IN(0x64) & 3) == 0; } /* output and input buffer empty */
static void key_send(uint8_t b)
{
    int i;
    UntrappedIO_OUT(0x64, 0xD2);
    for (i = 0; i < 2000 && (UntrappedIO_IN(0x64) & 2); ++i) {} /* controller took the command (a few us) */
    UntrappedIO_OUT(0x60, b);
}
static void key_stop(void) { K_Run = 0; K_Len = 0; K_Pos = 0; K_Phase = 0; K_OutN = K_OutPos = 0; }

static void key_tick(uint64_t t)
{
    if (!K_Run || !D_CpsI) return;
    if (t < K_WaitUntil) return;
    if (K_OutPos < K_OutN)                         /* bytes of a key event still to go */
    {
        if (!kbc_free())
        {
            if (!K_Stuck) K_Stuck = t;
            else if (t - K_Stuck > 3 * D_CpsI) key_stop();   /* nothing reads the keyboard */
            return;
        }
        K_Stuck = 0;
        key_send(K_Out[K_OutPos++]);
        /* hold each key (and the gap after it) 500 ms: games such as Duke 3D read the key state once per frame,
         * so a short press (beta.34 held 60 ms) could fall between two frames and be lost - on a slow machine or
         * a busy screen a frame can take a few hundred ms. Duke acts once per press however long it is held. */
        if (K_OutPos >= K_OutN) K_WaitUntil = t + D_CpsI / 2;
        return;
    }
    if (K_Phase == 1)                              /* press sent: now the release */
    {
        int n = K_OutN; K_OutN = 0; K_OutPos = 0;
        if (n == 2) K_Out[K_OutN++] = 0xE0;
        K_Out[K_OutN++] = (uint8_t)(K_Out[n - 1] | 0x80);
        K_Phase = 2;
        return;
    }
    K_Phase = 0;
    if (K_Pos >= K_Len) { key_stop(); return; }
    {
        uint8_t c = K_Scr[K_Pos];
        if (c == 0x80 && K_Pos + 1 < K_Len) { K_WaitUntil = t + (uint64_t)K_Scr[K_Pos + 1] * D_CpsI / 10; K_Pos += 2; return; }
        if (c == 0x81) { if (DIAG_KeyEnd) ++K_Pos; return; }
        if (c == 0x83 && K_Pos + 1 < K_Len) { K_OutN = 0; K_Out[K_OutN++] = 0xE0; K_Out[K_OutN++] = K_Scr[K_Pos + 1] & 0x7F; K_Pos += 2; }
        else if (c >= 1 && c <= 0x7F) { K_OutN = 0; K_Out[K_OutN++] = c; ++K_Pos; }
        else { key_stop(); return; }
        K_OutPos = 0; K_Phase = 1;
    }
}

/* ---------------------------------------------------------------- triple Esc (GAMETEST: stop the run)
 * While GAMETEST has the watch on (0xAD; 0xAE = off), SBEMU looks at the keyboard data port from its
 * sound interrupt - only when the controller's buffer is empty, i.e. the game has already taken the
 * byte, so no key is ever stolen. Three Esc presses within 2 s: the run is marked stopped and the quit
 * keys of the key script are sent at once. 0xAF makes the MPU data port return "E", stopped (0/1). */
static int E_Watch, E_Count, E_Last, E_Stopped;
static uint64_t E_First;
static uint8_t E_Q[2]; static int E_QLen, E_QPos;

static void esc_tick(uint64_t t)
{
    uint8_t st = UntrappedIO_IN(0x64), b;
    if ((st & 1) || (st & 0x20)) return;                 /* a byte is waiting for the game, or it is the touchpad's */
    b = UntrappedIO_IN(0x60);                            /* the last byte the game read */
    if (b == 0x01 && E_Last != 0x01)                     /* Esc pressed */
    {
        if (!E_Count || !D_CpsI || t - E_First > 2 * D_CpsI) { E_Count = 0; E_First = t; }
        if (++E_Count >= 3 && !E_Stopped) { E_Stopped = 1; DIAG_KeyEnd = 1; }   /* quit keys go out now */
    }
    E_Last = b;
}

static void ovl_tick(void);
void DIAG_IsrBegin(void)
{
    uint64_t t;
    ovl_tick();
    if (K_Run) key_tick(DIAG_Tsc());
    if (E_Watch) esc_tick(DIAG_Tsc());
    if (!DIAG_Armed) return;
    t = DIAG_Tsc();
    if (D_LimitSecs && D_CpsI && t - D_ArmTsc >= (uint64_t)D_LimitSecs * D_CpsI) /* time is up: keep this report */
    {
        snapshot(); D_Frozen = 1; DIAG_Armed = 0; D_LimitSecs = 0; DIAG_KeyEnd = 1;
        return;
    }
    if (!D_StretchStart || !D_CpsI || (t - D_IsrLastEnd) > D_CpUs * 50) /* gap > 50 us */
    {
        close_stretch();
        D_StretchStart = t;
    }
    if ((D_IsrCount & 15) == 0) sample_pit();
    /* how many ticks the game's timer should have produced, at the rate in force (only while a game
     * has sped the timer up: the plain 18.2 Hz DOS prompt does not count) */
    if (D_LastBegin && D_PitDiv && D_PitDiv < 65000 && D_CpsI)
    {
        uint64_t period = period_cycles(D_PitDiv);
        if (period) D_ExpectMilli += (t - D_LastBegin) * 1000u / period;
    }
    D_LastBegin = t;
    D_IsrT0 = DIAG_Tsc();
}

void DIAG_IsrEnd(int frames)
{
    uint64_t t, d; uint32_t u;
    if (!DIAG_Armed || !D_IsrT0) return;
    t = DIAG_Tsc(); d = t - D_IsrT0; u = us(d);
    ++D_IsrCount; D_IsrTotal += d; if (u > D_IsrMaxUs) D_IsrMaxUs = u;
    if (u > 1000) ++D_IsrHist[0];
    if (u > 2000) ++D_IsrHist[1];
    if (u > 5000) ++D_IsrHist[2];
    if (u > 10000) ++D_IsrHist[3];
    if ((uint32_t)frames > D_FramesMax) D_FramesMax = (uint32_t)frames;
    D_IsrLastEnd = t;
}

void DIAG_VirqBegin(void) { if (DIAG_Armed) D_VirqT0 = DIAG_Tsc(); }
void DIAG_VirqEnd(void)
{
    uint64_t d; uint32_t u;
    if (!DIAG_Armed || !D_VirqT0) return;
    d = DIAG_Tsc() - D_VirqT0; u = us(d); D_VirqT0 = 0;
    ++D_VirqCount; D_VirqTotal += d; if (u > D_VirqMaxUs) D_VirqMaxUs = u;
}

void DIAG_Render(uint64_t cycles, int frames, int freq, int synth)
{
    uint32_t u;
    if (!DIAG_Armed) return;
    u = us(cycles);
    ++D_RenderCount; D_RenderTotal += cycles; if (u > D_RenderMaxUs) D_RenderMaxUs = u;
    D_Frames += (uint32_t)frames; D_Freq = (uint32_t)freq; D_Synth = (uint32_t)synth;
}

void DIAG_Trap(uint32_t port)
{
    int c;
    if (!DIAG_Armed) return;
    if (port >= 0x330 && port <= 0x331) c = 0;                         /* MPU-401 */
    else if (port >= 0x220 && port <= 0x28F) c = 1;                    /* Sound Blaster (any base) */
    else if (port >= 0x388 && port <= 0x38B) c = 2;                    /* OPL */
    else if (port == 0x42 || port == 0x43 || port == 0x61) c = 3;      /* PIT / PC speaker */
    else if (port == 0x20 || port == 0x21 || port == 0xA0 || port == 0xA1) c = 4; /* PIC */
    else c = 5;                                                        /* DMA and others */
    ++D_Traps[c];
}

void DIAG_Fifo(int fill, uint32_t under_total)
{
    if (!DIAG_Armed) return;
    if (!D_UnderInit) { D_UnderBase = under_total; D_UnderInit = 1; }
    D_UnderNow = under_total;
    if (D_FifoMin < 0 || fill < D_FifoMin) D_FifoMin = fill;
}
uint64_t DIAG_CyclesPerSec(void) { return D_CpsI; }
void DIAG_BiosClock(uint64_t e, uint64_t g)
{
    if (!DIAG_Armed) return;
    if (!D_BiosInit) { D_BiosExp0 = e; D_BiosGot0 = g; D_BiosInit = 1; }
    D_BiosExp = e - D_BiosExp0; D_BiosGot = g - D_BiosGot0;
}
void DIAG_TimerReplayed(uint32_t n) { if (DIAG_Armed) D_Replayed += n; }
void DIAG_Midi(int dropped) { if (DIAG_Armed) { ++D_MidiBytes; if (dropped) ++D_MidiDropped; } }
void DIAG_SetSynthInfo(int gov_level, int fx_on, int voices) { D_GovLevel = (uint32_t)gov_level; D_Fx = (uint32_t)fx_on; D_Voices = (uint32_t)voices; }

static void put(int i, uint32_t v) { D_Buf[4*i] = (uint8_t)v; D_Buf[4*i+1] = (uint8_t)(v >> 8); D_Buf[4*i+2] = (uint8_t)(v >> 16); D_Buf[4*i+3] = (uint8_t)(v >> 24); }

static void snapshot(void)
{
    int n = 0, i;
    uint64_t now = DIAG_Tsc();
    close_stretch(); D_StretchStart = 0;
    put(n++, 0x47444253UL);                     /* "SBDG" */
    put(n++, 5);                                /* layout version */
    put(n++, (uint32_t)(D_CpsI / 1000));        /* kHz */
    put(n++, us(now - D_ArmTsc) / 1000);        /* ms since armed */
    put(n++, D_IsrCount);
    put(n++, us(D_IsrTotal) / 1000);            /* ms */
    put(n++, D_IsrMaxUs);
    for (i = 0; i < 4; ++i) put(n++, D_IsrHist[i]);
    put(n++, D_StretchMaxUs);
    put(n++, (uint32_t)D_StretchCount);
    put(n++, D_PitDiv);
    put(n++, D_PitMax);
    put(n++, D_PitSamples);
    put(n++, (uint32_t)D_LostMilli);
    put(n++, D_RenderCount);
    put(n++, us(D_RenderTotal) / 1000);         /* ms */
    put(n++, D_RenderMaxUs);
    put(n++, D_Frames);
    put(n++, D_Freq);
    put(n++, D_Synth);
    put(n++, D_VirqCount);
    put(n++, us(D_VirqTotal) / 1000);           /* ms */
    put(n++, D_VirqMaxUs);
    for (i = 0; i < 6; ++i) put(n++, D_Traps[i]);
    put(n++, D_MidiBytes);
    put(n++, D_MidiDropped);
    put(n++, D_GovLevel);
    put(n++, D_Fx);
    put(n++, D_Voices);
    put(n++, D_FramesMax);
    put(n++, D_PitFastest);
    put(n++, (uint32_t)D_ExpectMilli);
    put(n++, D_FifoMin < 0 ? 0xFFFFFFFFUL : (uint32_t)D_FifoMin);
    put(n++, D_UnderInit ? D_UnderNow - D_UnderBase : 0);
    put(n++, D_Replayed);
    put(n++, (uint32_t)(D_BiosExp / 1000));     /* DOS clock ticks that should have passed while the game ran */
    put(n++, (uint32_t)(D_BiosGot / 1000));     /* DOS clock ticks that did pass */
    D_BufLen = 4 * n; D_BufPos = 0;
}


int DIAG_Command(uint8_t cmd)
{
    if (K_Rx)                                                     /* receiving a key script */
    {
        if (cmd == 0x00) { K_Rx = 0; K_Pos = 0; K_Phase = 0; K_OutN = K_OutPos = 0; K_WaitUntil = 0; K_Stuck = 0; K_Run = K_Len > 0; }
        else if (K_Len < K_MAX) K_Scr[K_Len++] = cmd;              /* (too long: the rest is ignored) */
        return 1;
    }
    if (cmd == 0xAB) { key_stop(); DIAG_KeyEnd = 0; K_Rx = 1; return 1; } /* a key script follows */
    if (cmd == 0xAD) { E_Watch = 1; E_Count = 0; E_Last = 0; E_Stopped = 0; return 1; } /* watch for triple Esc */
    if (cmd == 0xAE) { E_Watch = 0; return 1; }
    if (cmd == 0xAF) { E_Q[0] = 'E'; E_Q[1] = (uint8_t)E_Stopped; E_QLen = 2; E_QPos = 0; return 1; }
    if (cmd == 0xAC) { key_stop(); K_Rx = 0; return 1; }          /* clear it */
    if (D_WantSecs) { D_WantSecs = 0; D_LimitSecs = cmd; return 1; }  /* the byte after 0xA9 */
    if (cmd == 0xA9) { D_WantSecs = 1; return 1; }
    if (cmd == 0xA7) { reset(); D_Frozen = 0; DIAG_Armed = 1; return 1; } /* start / restart measuring */
    if (cmd == 0xA6) { if (D_Frozen) D_BufPos = 0; else snapshot(); return 1; } /* take a report */
    if (cmd == 0xA8) { DIAG_Armed = 0; D_LimitSecs = 0; return 1; }  /* stop */
    if (cmd == 0xAA) { if (!D_Frozen) { snapshot(); D_Frozen = 1; } DIAG_Armed = 0; D_LimitSecs = 0; DIAG_KeyEnd = 1; return 1; } /* stop and keep the report */
    return 0;
}

int DIAG_ReadByte(uint8_t* b)
{
    if (E_QPos < E_QLen) { *b = E_Q[E_QPos++]; return 1; }   /* the answer to 0xAF comes first */
    if (D_BufPos >= D_BufLen) return 0;
    *b = D_Buf[D_BufPos++];
    return 1;
}


/* ---------------------------------------------------------------- SC-55 front panel overlay
 * Ctrl+Alt+F12 inside a game (or at the DOS prompt) shows the emulated module's real LCD - part,
 * instrument, level, pan, reverb, chorus, key shift, MIDI channel and the 16 x 16 dot level display -
 * in the top right corner; Ctrl+Alt+F12 again hides it. Drawn ~25 times a second from the sound
 * interrupt, on top of whatever the game draws:
 *   text modes (40/80 columns, mono)   4 rows of characters, the cells under it are put back on hide
 *   320x200x256 (mode 13h, linear)     a 120x56 pixel panel in the SC-55's own 5x7 LCD font
 * Other graphics (mode-X, VESA) are left alone: nothing is drawn there. The keys are only watched,
 * never taken from the game: the keyboard port is read only when the game has already read it. */
extern uint8_t lcd_font[240][10];                   /* HD44780 font of the LCD (Nuked-SC55 core) */
void VMPU_PanelSnapshot(unsigned char* out);         /* vmpu_sc55.c: 'L', on, synth, load lo/hi, level, LCD 80, CG 64 */
static int O_On, O_Ctrl, O_Alt, O_Last = -1, O_Kind, O_X, O_Cols, O_Rows = 4;
static uint32_t O_Base;
static uint64_t O_Next;
static uint16_t O_Save[4][40];
static uint8_t O_Fg, O_Bg;
static unsigned char O_P[150];

static int ovl_chain4(void)
{
    uint8_t idx = UntrappedIO_IN(0x3C4), v;
    UntrappedIO_OUT(0x3C4, 4); v = UntrappedIO_IN(0x3C5); UntrappedIO_OUT(0x3C4, idx); /* the game's index put back */
    return (v & 8) != 0;
}
/* 1 = text, 2 = mode 13h linear, 0 = something we do not draw on */
static int ovl_kind(void)
{
    uint8_t m = DPMI_LoadB(0x449);
    if (m <= 3 || m == 7) return 1;
    if (m == 0x13 && ovl_chain4()) return 2;
    return 0;
}
/* the LCD dot at row r (0 top..15), column c (0 left..15) of the level display */
static int ovl_dot(const unsigned char* d, const unsigned char* cg, int r, int c)
{
    int j = c / 5, k = c % 5; uint8_t ch = d[20 + j + (r >= 8 ? 40 : 0)];
    const uint8_t* f = ch >= 16 ? lcd_font[ch - 16] : &cg[(ch & 7) * 8];
    if (j == 3 && k > 0) return 0;
    return (f[r & 7] >> (4 - k)) & 1;
}
static char ovl_chr(uint8_t ch) { return ch >= 32 && ch < 127 ? (char)ch : ' '; }

/* the DAC colours closest to the LCD's amber and to black (13h only, read once when it is switched on) */
static void ovl_colours(void)
{
    int i, best = 1 << 30, dark = 1 << 30;
    for (i = 0; i < 256; i++)
    {
        int r, g, b, sa, sd;
        UntrappedIO_OUT(0x3C7, (uint8_t)i);
        r = UntrappedIO_IN(0x3C9); g = UntrappedIO_IN(0x3C9); b = UntrappedIO_IN(0x3C9);
        sa = (r - 60) * (r - 60) + (g - 38) * (g - 38) + (b - 6) * (b - 6);
        sd = r + g + b;
        if (sa < best) { best = sa; O_Bg = (uint8_t)i; }
        if (sd < dark) { dark = sd; O_Fg = (uint8_t)i; }
    }
}

static void ovl_text_save(int restore)
{
    int r, c;
    for (r = 0; r < O_Rows; r++) for (c = 0; c < 39; c++)
    {
        uint32_t a = O_Base + ((uint32_t)r * O_Cols + O_X + c) * 2;
        if (restore) DPMI_StoreW(a, O_Save[r][c]); else O_Save[r][c] = DPMI_LoadW(a);
    }
}
static void ovl_text(void)
{
    const unsigned char *d = O_P + 6, *cg = O_P + 86;
    char line[4][40]; int r, c;
    memset(line, ' ', sizeof line);
    for (c = 0; c < 3; c++) line[0][1 + c] = ovl_chr(d[c]);
    for (c = 0; c < 16; c++) line[0][5 + c] = ovl_chr(d[3 + c]);
    memcpy(&line[1][1], "LV", 2); for (c = 0; c < 3; c++) line[1][4 + c] = ovl_chr(d[40 + c]);
    memcpy(&line[1][9], "PN", 2); for (c = 0; c < 3; c++) line[1][12 + c] = ovl_chr(d[43 + c]);
    memcpy(&line[2][1], "RV", 2); for (c = 0; c < 3; c++) line[2][4 + c] = ovl_chr(d[49 + c]);
    memcpy(&line[2][9], "CH", 2); for (c = 0; c < 3; c++) line[2][12 + c] = ovl_chr(d[46 + c]);
    memcpy(&line[3][1], "KY", 2); for (c = 0; c < 3; c++) line[3][4 + c] = ovl_chr(d[52 + c]);
    memcpy(&line[3][9], "MI", 2); for (c = 0; c < 3; c++) line[3][12 + c] = ovl_chr(d[55 + c]);
    for (r = 0; r < 4; r++)
        for (c = 0; c < 39; c++)
        {
            uint8_t ch = (uint8_t)line[r][c], at = (c == 1 || c == 2 || c == 9 || c == 10) && r ? 0x6E : 0x60;
            if (c >= 22 && c < 38)
            {   /* 4 dot rows per text row: upper half = dots 0-1, lower half = dots 2-3 */
                int col = c - 22, up = ovl_dot(d, cg, r * 4, col) | ovl_dot(d, cg, r * 4 + 1, col);
                int lo = ovl_dot(d, cg, r * 4 + 2, col) | ovl_dot(d, cg, r * 4 + 3, col);
                ch = up && lo ? 0xDB : up ? 0xDF : lo ? 0xDC : 0xFA; at = ch == 0xFA ? 0x67 : 0x60;
            }
            DPMI_StoreW(O_Base + ((uint32_t)r * O_Cols + O_X + c) * 2, (uint16_t)(at << 8 | ch));
        }
}
static void ovl_px(int x, int y, uint8_t c) { DPMI_StoreB(0xA0000 + (uint32_t)y * 320 + x, c); }
static void ovl_glyph(int x, int y, uint8_t ch)
{
    const uint8_t* f = ch >= 16 ? lcd_font[ch - 16] : lcd_font[0];
    int r, k;
    if (ch < 32 || ch >= 127) ch = ' ', f = lcd_font[' ' - 16];
    for (r = 0; r < 7; r++) for (k = 0; k < 5; k++) ovl_px(x + k, y + r, (f[r] >> (4 - k)) & 1 ? O_Fg : O_Bg);
}
static void ovl_13h(void)
{
    const unsigned char *d = O_P + 6, *cg = O_P + 86;
    static const uint8_t f2[] = { 40, 41, 42, 43, 44, 45, 49, 50, 51, 46, 47, 48 };
    int x0 = 320 - 124, y0 = 2, x, y, r, c;
    for (y = 0; y < 56; y++) for (x = 0; x < 122; x++) ovl_px(x0 + x, y0 + y, (x == 0 || y == 0 || x == 121 || y == 55) ? O_Fg : O_Bg);
    for (c = 0; c < 3; c++) ovl_glyph(x0 + 2 + c * 6, y0 + 2, d[c]);
    for (c = 0; c < 16; c++) ovl_glyph(x0 + 24 + c * 6, y0 + 2, d[3 + c]);
    for (c = 0; c < 12; c++) ovl_glyph(x0 + 2 + (c / 3) * 20 + (c % 3) * 6, y0 + 12, d[f2[c]]);
    for (r = 0; r < 16; r++) for (c = 0; c < 16; c++)
        if (ovl_dot(d, cg, r, c)) { ovl_px(x0 + 37 + c * 3, y0 + 22 + r * 2, O_Fg); ovl_px(x0 + 38 + c * 3, y0 + 22 + r * 2, O_Fg); }
}

/* MT-32: its one-line 20-character LCD */
static void ovl_text_mt32(void)
{
    int c;
    for (c = 0; c < 39; c++)
    {
        uint8_t ch = ' ', at = 0x60;
        if (c >= 1 && c <= 6) { ch = (uint8_t)"MT-32 "[c - 1]; at = 0x6E; }
        else if (c >= 8 && c < 28) ch = (uint8_t)ovl_chr(O_P[6 + c - 8]);
        DPMI_StoreW(O_Base + (uint32_t)(O_X + c) * 2, (uint16_t)(at << 8 | ch));
    }
}
static void ovl_13h_mt32(void)
{
    int x0 = 320 - 128, y0 = 2, x, y, c;
    for (y = 0; y < 11; y++) for (x = 0; x < 126; x++) ovl_px(x0 + x, y0 + y, (x == 0 || y == 0 || x == 125 || y == 10) ? O_Fg : O_Bg);
    for (c = 0; c < 20; c++) ovl_glyph(x0 + 3 + c * 6, y0 + 2, O_P[6 + c]);
}

static void ovl_tick(void)
{
    uint8_t st = UntrappedIO_IN(0x64), b;
    uint64_t t;
    if (!(st & 1) && !(st & 0x20))
    {
        b = UntrappedIO_IN(0x60);
        if (b != O_Last)
        {
            O_Last = b;
            if (b == 0x1D) O_Ctrl = 1; else if (b == 0x9D) O_Ctrl = 0;
            else if (b == 0x38) O_Alt = 1; else if (b == 0xB8) O_Alt = 0;
            else if (b == 0x58 && O_Ctrl && O_Alt)
            {
                if (O_On && O_Kind == 1 && ovl_kind() == 1) ovl_text_save(1);   /* hide: put the text back */
                O_On = !O_On; O_Kind = 0;
            }
        }
    }
    if (!O_On) return;
    t = DIAG_Tsc();
    if (D_CpsI && t < O_Next) return;
    O_Next = t + (D_CpsI ? D_CpsI / 25 : 0);
    {
        int k = ovl_kind();
        if (!k) { O_Kind = 0; return; }
        VMPU_PanelSnapshot(O_P);
        if (O_P[2] == 1) return;                        /* SoundFont: there is no front panel */
        if (k != O_Kind)
        {   /* first frame in this mode */
            O_Kind = k;
            O_Rows = O_P[2] == 2 ? 1 : 4;
            if (k == 1)
            {
                uint8_t m = DPMI_LoadB(0x449);
                O_Cols = DPMI_LoadW(0x44A); if (O_Cols < 40 || O_Cols > 200) O_Cols = 80;
                O_X = O_Cols - 39; if (O_X < 0) O_X = 0;
                O_Base = (m == 7 ? 0xB0000 : 0xB8000) + DPMI_LoadW(0x44E);
                ovl_text_save(0);
            }
            else if (k == 2) ovl_colours();
        }
        if (O_P[2] == 2) { if (k == 1) ovl_text_mt32(); else ovl_13h_mt32(); }
        else if (k == 1) ovl_text(); else ovl_13h();
    }
}
