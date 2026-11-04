/* vmpu_mt32.cpp - Roland MT-32 / CM-32L back end for the virtual MPU-401 (Munt's libmt32emu, LGPL-2.1+).
 *
 * Used instead of the SC-55 when SBEMU is started with /VMT32 (auto, MT32 or CM32L). The ROM dumps are
 * found in the SC-55 ROM folder by content (SHA-1, through Munt's ROM table), so file names do not
 * matter; full images and split pairs (two halves, or even/odd chips) both work.
 *
 * Threading is the same as the SC-55's: MIDI bytes written by the game (port trap) go into a lock-free
 * queue; the sound interrupt parses them with Munt's MIDI stream parser right before it renders, so the
 * synth is only ever touched from one place. Munt renders at its native 32 kHz (digital output, the
 * cheapest mode); a small linear resampler converts that to the sound card's rate without allocating
 * anything inside the interrupt.
 *
 * GPL-2.0-or-later (links LGPL-2.1+ libmt32emu). */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <dirent.h>
#include <new>
#include "mt32emu.h"
#include "vmpu_mt32.h"

using namespace MT32Emu;

namespace {
/* Munt's default handler printf()s debug lines and the LCD text games send ("WRITE-LCD: ...") - that would
 * run inside the sound interrupt and crash DOS. This one stays silent; the LCD is read with getDisplayState. */
class SilentReport : public ReportHandler2
{
public:
    void printDebug(const char*, va_list) override {}
    void showLCDMessage(const char*) override {}
    void onErrorControlROM() override {}
    void onErrorPCMROM() override {}
};
SilentReport g_report;
Synth* g_synth;
DefaultMidiStreamParser* g_parser;
char g_err[160] = "", g_model[64] = "";
int g_gain = 100;
volatile int g_gain_dirty = 1;

/* MIDI queue: game side writes, interrupt side reads */
const uint32_t QSIZE = 65536;
uint8_t g_q[QSIZE];
volatile uint32_t g_qh, g_qt;          /* head = read position (interrupt), tail = write position (game) */

/* resampler: B[0] is the last native frame of the previous call */
const int NBUF = 8192;
int16_t g_nat[(NBUF + 2) * 2];
uint32_t g_pos;                         /* 16.16 position from B[0] */
bool g_have0;

struct RomFile { char path[300]; FileStream* fs; const ROMInfo* info; };

bool has_prefix(const char* s, const char* p) { return strncmp(s, p, strlen(p)) == 0; }
} // namespace

extern "C" {

/* the Munt sources are compiled with printf/vprintf renamed to these: nothing may print from inside the
 * sound interrupt (the library prints only in "should never happen" cases) */
int mt32emu_silent_printf(const char*, ...) { return 0; }
int mt32emu_silent_vprintf(const char*, va_list) { return 0; }

const char* MT32_LastError(void) { return g_err; }
const char* MT32_ModelName(void) { return g_model; }
int MT32_IsLoaded(void) { return g_synth != NULL; }

/* want: "", "auto", "mt32" or "cm32l" */
int MT32_Init(const char* romdir, const char* want, int gain_percent, int verbose)
{
    static RomFile f[48];
    int n = 0, i, j;
    DIR* d;
    struct dirent* e;
    const ROMImage *ctrl = NULL, *pcm = NULL;
    const char* order[2][2] = { { "ctrl_mt32", "pcm_mt32" }, { "ctrl_cm32l", "pcm_cm32l" } };
    int first = 0, tries = 2;
    g_err[0] = 0;
    if (want && !strcasecmp(want, "cm32l")) { first = 1; tries = 1; }
    else if (want && !strcasecmp(want, "mt32")) { first = 0; tries = 1; }

    d = opendir(romdir);
    if (!d) { snprintf(g_err, sizeof g_err, "ROM folder %s not found", romdir); return 0; }
    while ((e = readdir(d)) != NULL && n < 48)
    {
        struct stat st;
        snprintf(f[n].path, sizeof f[n].path, "%s/%s", romdir, e->d_name);
        if (stat(f[n].path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        if (st.st_size != 32768 && st.st_size != 65536 && st.st_size != 131072 && st.st_size != 262144
            && st.st_size != 524288 && st.st_size != 1048576) continue;
        f[n].fs = new (std::nothrow) FileStream;
        if (!f[n].fs || !f[n].fs->open(f[n].path)) { delete f[n].fs; continue; }
        f[n].info = ROMInfo::getROMInfo(f[n].fs);
        if (!f[n].info) { delete f[n].fs; continue; }
        (void)verbose;
        n++;
    }
    closedir(d);

    for (int t = 0; t < tries && !(ctrl && pcm); t++)
    {
        const char* cp = order[(first + t) % 2][0];
        const char* pp = order[(first + t) % 2][1];
        ctrl = pcm = NULL;
        for (int kind = 0; kind < 2; kind++)
        {
            const char* pre = kind ? pp : cp;
            const ROMImage* img = NULL;
            /* control ROM preference: the versions games were written for first */
            static const char* const pref[] = { "ctrl_mt32_1_07", "ctrl_mt32_1_06", "ctrl_mt32_1_05", "ctrl_mt32_1_04",
                "ctrl_mt32_bluer", "ctrl_mt32_2_07", "ctrl_mt32_2_06", "ctrl_mt32_2_04", "ctrl_mt32_2_03",
                "ctrl_cm32l_1_02", "ctrl_cm32l_1_00", "ctrl_cm32ln", "pcm_mt32", "pcm_cm32l", "", NULL }; /* "" = anything else */
            for (int pi = 0; pref[pi] && !img; pi++)
            for (i = 0; i < n && !img; i++)
            {
                if (!has_prefix(f[i].info->shortName, pre)) continue;
                if (pref[pi][0] && !has_prefix(f[i].info->shortName, pref[pi])) continue;
                if (f[i].info->pairType == ROMInfo::Full) img = ROMImage::makeROMImage(f[i].fs);
                else
                    for (j = 0; j < n && !img; j++)
                        if (j != i && (f[i].info->pairROMInfo == f[j].info || f[j].info->pairROMInfo == f[i].info))
                            img = ROMImage::makeROMImage(f[i].fs, f[j].fs);
            }
            if (kind) pcm = img; else ctrl = img;
        }
        if (!(ctrl && pcm))
        {
            if (ctrl) ROMImage::freeROMImage(ctrl);
            if (pcm) ROMImage::freeROMImage(pcm);
            ctrl = pcm = NULL;
        }
    }
    if (!ctrl || !pcm)
    {
        snprintf(g_err, sizeof g_err, "no complete %s ROM pair (control + PCM) in %s",
                 tries == 1 ? (first ? "CM-32L" : "MT-32") : "MT-32 or CM-32L", romdir);
        for (i = 0; i < n; i++) delete f[i].fs;
        return 0;
    }
    g_synth = new (std::nothrow) Synth(&g_report);
    if (!g_synth || !g_synth->open(*ctrl, *pcm, DEFAULT_MAX_PARTIALS, AnalogOutputMode_DIGITAL_ONLY))
    {
        snprintf(g_err, sizeof g_err, "the MT-32 emulation could not start (out of memory?)");
        delete g_synth; g_synth = NULL;
    }
    else
    {
        snprintf(g_model, sizeof g_model, "%s", ctrl->getROMInfo()->description);
        g_parser = new (std::nothrow) DefaultMidiStreamParser(*g_synth, 32768); /* room for any sysex: no allocation inside the interrupt */
        if (!g_parser) { g_synth->close(); delete g_synth; g_synth = NULL; snprintf(g_err, sizeof g_err, "out of memory"); }
    }
    ROMImage::freeROMImage(ctrl);            /* the synth has its own copy now */
    ROMImage::freeROMImage(pcm);
    for (i = 0; i < n; i++) delete f[i].fs;
    if (!g_synth) return 0;
    g_gain = gain_percent; g_gain_dirty = 1;
    g_qh = g_qt = 0; g_pos = 0; g_have0 = false;
    return 1;
}

int MT32_PostByte(uint8_t b)
{
    uint32_t t = g_qt, nt = (t + 1) & (QSIZE - 1);
    if (nt == g_qh) return 0;                /* full: dropped, like a UART overrun */
    g_q[t] = b;
    g_qt = nt;
    return 1;
}
int MT32_Busy(void) { return ((g_qt - g_qh) & (QSIZE - 1)) > QSIZE - 1024; }
void MT32_SetGain(int percent) { g_gain = percent; g_gain_dirty = 1; }

static void feed(void)
{
    uint8_t buf[512];
    for (;;)
    {
        uint32_t h = g_qh, t = g_qt, n = 0;
        while (h != t && n < sizeof buf) { buf[n++] = g_q[h]; h = (h + 1) & (QSIZE - 1); }
        if (!n) break;
        g_qh = h;
        g_parser->parseStream(buf, n);
    }
}

/* frames of stereo 16-bit sound at rate; replaces what is in pcm */
void MT32_Render(int16_t* pcm, int frames, uint32_t rate)
{
    if (!g_synth || frames <= 0 || !rate) return;
    if (g_gain_dirty) { g_gain_dirty = 0; g_synth->setOutputGain(g_gain / 100.0f); }
    feed();
    const uint32_t nat = 32000;              /* DIGITAL_ONLY output rate */
    const uint32_t step = (uint32_t)(((uint64_t)nat << 16) / rate);
    while (frames > 0)
    {
        int chunk = frames > 2048 ? 2048 : frames;
        uint32_t last = g_pos + step * (uint32_t)(chunk - 1), after = g_pos + step * (uint32_t)chunk;
        uint32_t need = (last >> 16) + 1;
        if ((after >> 16) > need) need = after >> 16;
        if (!g_have0) { g_synth->render(g_nat, 1); g_have0 = true; }
        if (need > (uint32_t)NBUF) need = NBUF;
        g_synth->render(g_nat + 2, need);    /* frames 1..need after B[0] */
        for (int i = 0; i < chunk; i++)
        {
            uint32_t idx = g_pos >> 16, fr = g_pos & 0xFFFF;
            if (idx + 1 > need) idx = need - 1;
            const int16_t* a = g_nat + idx * 2;
            pcm[0] = (int16_t)(a[0] + (((int32_t)a[2] - a[0]) * (int32_t)fr >> 16));
            pcm[1] = (int16_t)(a[1] + (((int32_t)a[3] - a[1]) * (int32_t)fr >> 16));
            pcm += 2;
            g_pos += step;
        }
        uint32_t keep = g_pos >> 16;
        if (keep > need) keep = need;
        g_nat[0] = g_nat[keep * 2]; g_nat[1] = g_nat[keep * 2 + 1];
        g_pos -= keep << 16;
        frames -= chunk;
    }
}

/* the MT-32's 20-character LCD; returns 1 while the MIDI message LED is on */
int MT32_GetLCD(char* out21)
{
    char b[64];
    memset(out21, ' ', 20); out21[20] = 0;
    if (!g_synth) return 0;
    memset(b, 0, sizeof b);
    bool led = g_synth->getDisplayState(b, false);
    for (int i = 0; i < 20 && b[i]; i++) out21[i] = b[i];
    return led ? 1 : 0;
}

int MT32_Active(void) { return g_synth ? 1 : 0; }

} // extern "C"
