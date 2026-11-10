// w98main.cpp - SC55W.EXE, the Windows 98 side of NUKED SB-EMU.
//
// Loads SC55W.VXD (Sound Blaster 16 / OPL3 / MPU-401 for DOS boxes, plus the
// "NUKED SB-EMU" Windows MIDI device), reads its event and PCM rings and turns
// them into sound with the same synths as the DOS pack (SC-55, MT-32,
// SoundFont, DOSBox / Nuked OPL3). Settings are the DOS pack's SBOPTS.TXT.
// Lives in the tray; menu: Status, Settings, All notes off, Exit.
//
// Command line: /SETTINGS  open the settings window (starts the helper if needed)
//               /STATUS    open the status window
//               /EXIT      stop a running helper (used by the uninstaller)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <commdlg.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "engine.h"
#include "romscan.h"
#include "install98.h"
#include "pad.h"
#include "joyout.h"
#include "gameport.h"
#include <commctrl.h>
#include "resource.h"

#ifndef BUILD_ID
#define BUILD_ID 0
#endif

static const char APP_TITLE[] = "NUKED SB-EMU for Windows 98";
static const char WND_CLASS[] = "SC55W_Main";
#define WM_TRAY      (WM_APP + 1)
#define WM_STATECHG  (WM_APP + 2)
#define WM_REMOTE    (WM_APP + 3)     // wParam: 1 settings, 2 status, 3 exit

// ---------------------------------------------------------------------------
// paths
static std::string g_exedir, g_base, g_optpath, g_batpath, g_inipath;

static std::string dir_of(const std::string& p)
{
    size_t k = p.find_last_of("\\/");
    return k == std::string::npos ? std::string(".") : p.substr(0, k);
}
static bool is_dir(const std::string& p)
{
    DWORD a = GetFileAttributesA(p.c_str());
    return a != 0xFFFFFFFF && (a & FILE_ATTRIBUTE_DIRECTORY);
}
static bool is_file(const std::string& p)
{
    DWORD a = GetFileAttributesA(p.c_str());
    return a != 0xFFFFFFFF && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
static bool is_abs(const std::string& p)
{
    return (p.size() > 1 && p[1] == ':') || (!p.empty() && (p[0] == '\\' || p[0] == '/'));
}
static std::string resolve(const std::string& p)
{
    return p.empty() || is_abs(p) ? p : path_join(g_base, p);
}
static std::string short_path(const std::string& p)
{
    char b[MAX_PATH];
    DWORD n = GetShortPathNameA(p.c_str(), b, sizeof b);
    return n && n < sizeof b ? std::string(b) : p;
}

static void find_paths()
{
    char b[MAX_PATH];
    GetModuleFileNameA(NULL, b, sizeof b);
    g_exedir = dir_of(b);
    // C:\SC55\WIN98\SC55W.EXE -> the pack is C:\SC55 (ROMS, SBOPTS.TXT, SF2)
    std::string up = dir_of(g_exedir);
    if (is_dir(path_join(g_exedir, "ROMS")) || is_file(path_join(g_exedir, "SBOPTS.TXT"))) g_base = g_exedir;
    else if (is_dir(path_join(up, "ROMS")) || is_file(path_join(up, "SBOPTS.TXT"))) g_base = up;
    else g_base = g_exedir;
    g_optpath = path_join(g_base, "SBOPTS.TXT");
    g_batpath = path_join(g_base, "SBOPTS.BAT");
    g_inipath = path_join(g_exedir, "SC55W.INI");
}

// Windows-only settings (not SBEMU options, so they stay out of SBOPTS.TXT)
static int ini_int(const char* key, int def) { return GetPrivateProfileIntA("SC55W", key, def, g_inipath.c_str()); }

// Start-up step, kept in SC55W.INI [State] Step= and flushed to disk at once. If Windows
// froze during a start, the step is still set at the next boot: that start is skipped
// (no freeze loop) and the user is told where it stopped.
static void set_step(const char* step)
{
    WritePrivateProfileStringA("State", "Step", step, g_inipath.c_str());
    WritePrivateProfileStringA(NULL, NULL, NULL, g_inipath.c_str());   // flush the INI cache
}
static std::string get_step()
{
    char b[64];
    GetPrivateProfileStringA("State", "Step", "", b, sizeof b, g_inipath.c_str());
    return b;
}

// ---------------------------------------------------------------------------
// shared state
static HINSTANCE g_inst;
static HWND g_wnd, g_status_dlg, g_settings_dlg;
static CRITICAL_SECTION g_cs;
static Options g_opt;                 // current saved options (UI thread)
static Options g_pending;             // handed to the audio thread
static volatile LONG g_have_pending, g_panic, g_quit;
static bool g_boot;                 // started from the Run key (/BOOT): let Windows finish starting first
static HANDLE g_vxd = INVALID_HANDLE_VALUE;
static SC55W_SHARED* g_sh;
static DWORD g_vxd_build;
static std::string g_vxd_error;
static HANDLE g_thread;

// published by the audio thread (read by the UI under g_cs)
struct Published {
    std::string state = "Starting";   // short, for the tooltip
    std::string synth, synth_error;
    DWORD attach = 0;
    bool attached = false;
    int rate = 0, buf_ms = 0;
    std::string wave_error;
    EngineStats st;
    unsigned underruns = 0;
    double load = 0;
    bool shed = false;
} g_pub;

// SC55W.LOG next to the EXE: what happened at start-up, for troubleshooting
static void logf(const char* fmt, ...)
{
    static bool first = true;
    FILE* f = fopen(path_join(g_exedir, "SC55W.LOG").c_str(), first ? "w" : "a");
    first = false;
    if (!f) return;
    va_list a;
    va_start(a, fmt);
    fprintf(f, "[%8lu] ", GetTickCount());
    vfprintf(f, fmt, a);
    fputs("\r\n", f);
    va_end(a);
    fclose(f);
}

static void publish_state(const std::string& s)
{
    if (s == g_pub.state) return;
    logf("state: %s", s.c_str());
    EnterCriticalSection(&g_cs);
    g_pub.state = s;
    LeaveCriticalSection(&g_cs);
    PostMessageA(g_wnd, WM_STATECHG, 0, 0);
}

// ---------------------------------------------------------------------------
// VxD
static bool vxd_open()
{
    std::string p = short_path(path_join(g_exedir, "SC55W.VXD"));
    if (!is_file(p)) { g_vxd_error = "SC55W.VXD is missing from " + g_exedir; return false; }
    g_vxd = CreateFileA(("\\\\.\\" + p).c_str(), 0, 0, NULL, 0, FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (g_vxd == INVALID_HANDLE_VALUE) {
        char b[160];
        snprintf(b, sizeof b, "Windows could not load SC55W.VXD (error %lu).", GetLastError());
        g_vxd_error = b;
        return false;
    }
    DWORD v[2] = {0, 0}, got = 0;
    DeviceIoControl(g_vxd, IOCTL_SC55W_VERSION, NULL, 0, v, sizeof v, &got, NULL);
    if (v[0] != SC55W_IF_VERSION) {
        char b[160];
        snprintf(b, sizeof b, "SC55W.VXD is a different version (%lu, need %d). Restart Windows after updating.",
                 v[0], SC55W_IF_VERSION);
        g_vxd_error = b;
        CloseHandle(g_vxd);
        g_vxd = INVALID_HANDLE_VALUE;
        return false;
    }
    g_vxd_build = v[1];
    g_sh = (SC55W_SHARED*)VirtualAlloc(NULL, sizeof(SC55W_SHARED), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!g_sh) { g_vxd_error = "out of memory"; return false; }
    return true;
}

static SC55W_CONFIG config_for(const Options& o)
{
    SC55W_CONFIG c;
    memset(&c, 0, sizeof c);
    c.sb_port = (u16)o.sb_port;
    c.mpu_port = o.vmpu ? (u16)o.mpu_port : 0;
    c.irq = (u8)o.irq;
    c.dma8 = (u8)o.dma8;
    c.dma16 = o.sbtype >= 6 ? (u8)o.dma16 : 0;
    c.sbtype = (u8)o.sbtype;
    c.opl = (u8)(o.opl != 0);
    c.fixtc = (u8)(o.fixtc != 0);
    // DOS boxes get a game port (201h) and the BIOS joystick call (INT 15h AH=84h),
    // fed from the Windows joystick (SC55W.INI Joystick= / JoyBios=, 1 = on)
    c.joy = (u8)((ini_int("Joystick", 1) ? 1 : 0) | (ini_int("JoyBios", 1) ? 2 : 0) |
                 (ini_int("PadKeys", 1) ? 4 : 0) |     // pad buttons -> keys, layout per game
                 (GetPrivateProfileIntA("Joystick", "WindowsPort", 0, g_inipath.c_str()) ? 8 : 0));
    // [Joystick] Port=201 (200..20F): where the game port is, for PCs whose sound card has none
    char pb[16];
    GetPrivateProfileStringA("Joystick", "Port", "201", pb, sizeof pb, g_inipath.c_str());
    unsigned long port = strtoul(pb, NULL, 16);
    c.joy_port = (u16)(port >= 0x200 && port <= 0x20F ? port : 0x201);
    c.joy_type = (u8)GetPrivateProfileIntA("Joystick", "Type", 0, g_inipath.c_str());
    return c;
}

// (re)attach with new settings; called from the audio thread only
static DWORD vxd_attach(const Options& o)
{
    if (g_vxd == INVALID_HANDLE_VALUE || !g_sh) return 0;
    DWORD got = 0, st = 0;
    DeviceIoControl(g_vxd, IOCTL_SC55W_DETACH, NULL, 0, NULL, 0, &got, NULL);
    memset(g_sh, 0, sizeof *g_sh);
    g_sh->magic = SC55W_MAGIC;
    g_sh->if_version = SC55W_IF_VERSION;
    g_sh->size = sizeof *g_sh;
    g_sh->cfg = config_for(o);
    for (int i = 0x30; i <= 0x37; i++) g_sh->mixer[i] = 0xF8;
    DWORD addr = (DWORD)g_sh;
    if (!DeviceIoControl(g_vxd, IOCTL_SC55W_ATTACH, &addr, 4, &st, 4, &got, NULL)) st = ST_ERR_LOCK;
    return st;
}

static uint32_t vxd_now()
{
    if (g_vxd != INVALID_HANDLE_VALUE) {
        DWORD t = 0, got = 0;
        if (DeviceIoControl(g_vxd, IOCTL_SC55W_TIME, NULL, 0, &t, 4, &got, NULL) && got == 4) return t;
    }
    return timeGetTime() * 1000u;
}

static bool attach_ok(DWORD st) { return !(st & (ST_ERR_BLOCK | ST_ERR_LOCK | ST_ERR_BUSY)) && st != 0; }

// ---------------------------------------------------------------------------
// audio thread
static HWAVEOUT g_wo;
static HANDLE g_wev;
static std::vector<WAVEHDR> g_hdr;
static std::vector<std::vector<int16_t>> g_buf;
static int g_blk;

static void wave_close()
{
    if (!g_wo) return;
    waveOutReset(g_wo);
    for (auto& h : g_hdr) if (h.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(g_wo, &h, sizeof h);
    waveOutClose(g_wo);
    g_wo = NULL;
    g_hdr.clear();
    g_buf.clear();
}

static bool wave_open(int rate, int total_ms, std::string& err)
{
    WAVEFORMATEX f;
    memset(&f, 0, sizeof f);
    f.wFormatTag = WAVE_FORMAT_PCM;
    f.nChannels = 2;
    f.nSamplesPerSec = rate;
    f.wBitsPerSample = 16;
    f.nBlockAlign = 4;
    f.nAvgBytesPerSec = rate * 4;
    ResetEvent(g_wev);
    MMRESULT r = waveOutOpen(&g_wo, WAVE_MAPPER, &f, (DWORD_PTR)g_wev, 0, CALLBACK_EVENT);
    if (r != MMSYSERR_NOERROR) {
        char b[200], t[120] = "";
        waveOutGetErrorTextA(r, t, sizeof t);
        snprintf(b, sizeof b, "Cannot open the sound output at %d Hz: %s", rate, t);
        err = b;
        g_wo = NULL;
        return false;
    }
    int blk_ms = ini_int("BlockMs", 10);
    if (blk_ms < 5) blk_ms = 5;
    if (blk_ms > 50) blk_ms = 50;
    int n = total_ms / blk_ms;
    if (n < 3) n = 3;
    if (n > 32) n = 32;
    g_blk = rate * blk_ms / 1000;
    g_hdr.assign(n, WAVEHDR());
    g_buf.assign(n, std::vector<int16_t>((size_t)g_blk * 2, 0));
    for (int i = 0; i < n; i++) {
        WAVEHDR& h = g_hdr[i];
        memset(&h, 0, sizeof h);
        h.lpData = (LPSTR)g_buf[i].data();
        h.dwBufferLength = g_blk * 4;
        waveOutPrepareHeader(g_wo, &h, sizeof h);
        h.dwFlags |= WHDR_DONE;       // "free": filled by the loop below
    }
    err.clear();
    return true;
}

// SC55W.INI [SC55W] RecordWav=C:\OUT.WAV: also write everything played to a
// WAV file (troubleshooting). The file is restarted when the rate changes.
static FILE* g_rec;
static int g_rec_rate;
static uint32_t g_rec_frames;
static void rec_close()
{
    if (!g_rec) return;
    uint32_t bytes = g_rec_frames * 4, h[11];
    memcpy(&h[0], "RIFF", 4); h[1] = 36 + bytes; memcpy(&h[2], "WAVE", 4);
    memcpy(&h[3], "fmt ", 4); h[4] = 16; h[5] = 1 | (2 << 16); h[6] = g_rec_rate;
    h[7] = g_rec_rate * 4; h[8] = 4 | (16 << 16); memcpy(&h[9], "data", 4); h[10] = bytes;
    fseek(g_rec, 0, SEEK_SET);
    fwrite(h, 4, 11, g_rec);
    fclose(g_rec);
    g_rec = NULL;
}
static void rec_open(int rate)
{
    rec_close();
    char p[MAX_PATH] = "";
    GetPrivateProfileStringA("SC55W", "RecordWav", "", p, sizeof p, g_inipath.c_str());
    if (!p[0]) return;
    g_rec = fopen(p, "wb");
    if (!g_rec) return;
    g_rec_rate = rate;
    g_rec_frames = 0;
    char z[44] = {0};
    fwrite(z, 1, 44, g_rec);
}
static void rec_write(const int16_t* d, int frames)
{
    if (!g_rec) return;
    fwrite(d, 4, frames, g_rec);
    g_rec_frames += frames;
    if ((g_rec_frames & 0xFFFF) < (uint32_t)frames) fflush(g_rec);
}

// the options the engine sees: absolute paths, MIDI synth always present
// (the Windows MIDI device uses it even when the DOS MPU-401 is off)
static Options engine_opts(const Options& o)
{
    Options e = o;
    e.vmpu = 1;
    e.sf = resolve(o.sf);
    if (e.scahead < 20) e.scahead = 20;
    return e;
}

static DWORD WINAPI audio_main(LPVOID)
{
    // no timeBeginPeriod(1) here (CPU): on Windows 98 it reprograms the PC's timer to 1000 interrupts a
    // second for as long as SC55W runs. The sound loop waits on the wave-out event, not on Sleep; the 1 ms
    // timer is only switched on while it is needed: by SC55W.VXD while a DOS program plays sound (DMA),
    // and by the joystick thread while a game reads the pad.
    // normal priority while the synth boots (it takes seconds); the render
    // loop runs at "highest" so DOS boxes and games do not starve the sound
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
    Engine eng;
    Options cur;
    bool have = false, shed = false;
    SC55W_SHARED* sh = NULL;
    DWORD shed_since = 0, last_pub = 0, last_wave_try = 0;
    unsigned underruns = 0;
    double load = 0;
    LARGE_INTEGER qf;
    QueryPerformanceFrequency(&qf);
    std::string wave_err;
    int buf_ms = ini_int("BufferMs", 80);
    // CPU credit (as SBEMU's /VSCAUTO in DOS, beta.68): the render thread may use at most
    // CpuMax % of the time, so a synth that cannot keep up makes the music lag or break up
    // instead of starving Windows (SC55W.INI CpuMax=, 20-95, default 60)
    int cpu_max = ini_int("CpuMax", 60);
    if (cpu_max < 20) cpu_max = 20;
    if (cpu_max > 95) cpu_max = 95;
    double credit = 0.1;                        // seconds of render time we may still spend
    LARGE_INTEGER credit_t;
    QueryPerformanceCounter(&credit_t);
    int prio = THREAD_PRIORITY_NORMAL;
    unsigned throttled = 0;
    DWORD trace_start = GetTickCount(), trace_last = 0;
    DWORD step_ok_at = 0;                       // 20 s after the sound runs the start counts as good
    if (buf_ms < 20) buf_ms = 20;
    if (buf_ms > 500) buf_ms = 500;
    const bool out_sleep = ini_int("OutputSleep", 1) != 0;
    // A DOS game full screen gets most of the CPU in time slices; this thread then only runs in short turns
    // and an 80 ms output buffer runs dry between them (Blood: ~40 ms of sound, ~80 ms of silence). While
    // DOS DMA sound plays and the output keeps running dry, the buffer steps up to 160, then 300 ms; a minute
    // without DMA sound brings it back. SC55W.INI BufferAuto=0 keeps BufferMs as it is.
    const int buf_base = buf_ms;
    const bool buf_auto = ini_int("BufferAuto", 1) != 0;
    DWORD dry_t = 0, last_dma = 0; unsigned dry_u = 0;

    while (!g_quit) {
        if (InterlockedExchange(&g_have_pending, 0)) {
            EnterCriticalSection(&g_cs);
            Options want = g_pending;
            LeaveCriticalSection(&g_cs);
            SC55W_CONFIG a = config_for(want);
            static SC55W_CONFIG attached;            // what the VxD has (INI joystick keys included)
            if (!have || memcmp(&a, &attached, sizeof a)) {
                attached = a;
                publish_state("Connecting the Sound Blaster");
                set_step("attach");
                DWORD st = vxd_attach(want);
                sh = attach_ok(st) ? g_sh : NULL;
                EnterCriticalSection(&g_cs);
                g_pub.attach = st;
                g_pub.attached = sh != NULL;
                LeaveCriticalSection(&g_cs);
                logf("attach %X:%d:%d:%d type %d mpu %X -> status %05lX", want.sb_port, want.irq, want.dma8,
                     want.dma16, want.sbtype, want.vmpu ? want.mpu_port : 0, st);
            }
            Options eo = engine_opts(want);
            if (!have || want.rate != cur.rate || !eng.apply_live(eo)) {
                wave_close();
                if (g_boot && !have) {
                    // started with Windows: the SC-55 boot is heavy, so let the other startup
                    // programs finish first (SC55W.INI StartDelay=, seconds, 0 = off)
                    int d = ini_int("StartDelay", 15);
                    if (d > 120) d = 120;
                    if (d > 0) {
                        publish_state("Waiting for Windows to finish starting");
                        logf("start delay %d s", d);
                        for (int i = 0; i < d * 4 && !g_quit; i++) Sleep(250);
                    }
                }
                publish_state("Starting the synth");
                set_step("synth");
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
                eng.setup(eo, resolve(want.scrom), want.rate, eo.scahead);
                rec_open(want.rate);
                shed = false;
                EnterCriticalSection(&g_cs);
                g_pub.synth = eng.midi ? eng.midi->describe() : "none";
                g_pub.synth_error = eng.midi_error;
                g_pub.rate = want.rate;
                LeaveCriticalSection(&g_cs);
                logf("synth: %s %s (rate %d, ahead %d)", g_pub.synth.c_str(), eng.midi_error.c_str(), want.rate, eo.scahead);
                last_wave_try = 0;
            }
            cur = want;
            have = true;
            publish_state("Running");
        }
        if (!have) { Sleep(20); continue; }

        if (!g_wo) {
            DWORD now = GetTickCount();
            if (last_wave_try && now - last_wave_try < 2000) {
                // no output: keep rendering in real time so the rings are drained
                // (and SC55W.INI RecordWav= still gets the sound)
                static int16_t tmp[4096 * 2];
                static DWORD last;
                DWORD el = last ? now - last : 50;
                last = now;
                if (el > 100) el = 100;             // cannot keep up: skip ahead
                int n = (int)((uint64_t)el * cur.rate / 1000);
                if (n > 4096) n = 4096;
                if (out_sleep && !g_rec && eng.can_sleep(sh)) {
                    // CPU: silent and nothing waiting - nothing to drain; a slow tick for the VxD's clock
                    if (!eng.st.out_asleep) eng.st.out_asleep = true;
                    vxd_now();
                    Sleep(250);
                    last = 0;
                    continue;
                }
                if (eng.st.out_asleep) { eng.st.out_asleep = false; eng.wake_output(); }
                if (n > 0) { eng.render(sh, vxd_now(), tmp, n); rec_write(tmp, n); }
                Sleep(25);
                continue;
            }
            last_wave_try = now;
            if (step_ok_at == 0) set_step("wave");
            if (!wave_open(cur.rate, buf_ms, wave_err)) {
                if (wave_err != g_pub.wave_error) logf("%s", wave_err.c_str());
                EnterCriticalSection(&g_cs);
                g_pub.wave_error = wave_err;
                LeaveCriticalSection(&g_cs);
                publish_state("No sound output");
                continue;
            }
            EnterCriticalSection(&g_cs);
            g_pub.wave_error.clear();
            g_pub.buf_ms = buf_ms;
            LeaveCriticalSection(&g_cs);
            publish_state("Running");
            prio = THREAD_PRIORITY_ABOVE_NORMAL;
            SetThreadPriority(GetCurrentThread(), prio);
        }

        if (InterlockedExchange(&g_panic, 0)) {
            if (eng.midi) eng.midi->all_notes_off();
            if (eng.opl) eng.opl->reset();
        }

        // CPU: while nothing makes a sound the sound output is stopped (no card interrupts, no mixing, and
        // this loop does not run 100 times a second); input in the VxD's rings (a DOS program, Windows MIDI)
        // starts it again within ~20 ms. SC55W.INI OutputSleep=0 keeps it running.
        bool woke = false;
        if (eng.st.out_asleep) {
            if (eng.has_input(sh)) {
                eng.st.out_asleep = false;
                eng.wake_output();
                QueryPerformanceCounter(&credit_t);
                woke = true;
            } else {
                static DWORD tick;
                Sleep(20);
                DWORD t = GetTickCount();
                if (t - tick >= 250) { tick = t; vxd_now(); }   // the VxD drops its 1 ms timer from this call
            }
        }
        if (!eng.st.out_asleep) {
        WaitForSingleObject(g_wev, 50);
        int queued = 0;
        for (auto& h : g_hdr) if (!(h.dwFlags & WHDR_DONE)) queued++;
        if (queued == 0 && !woke) underruns++;
        for (size_t i = 0; i < g_hdr.size() && !g_quit; i++) {
            WAVEHDR& h = g_hdr[i];
            if (!(h.dwFlags & WHDR_DONE)) continue;
            LARGE_INTEGER t0, t1;
            QueryPerformanceCounter(&t0);
            credit += (double)(t0.QuadPart - credit_t.QuadPart) / qf.QuadPart * cpu_max / 100.0;
            credit_t = t0;
            if (credit > 0.1) credit = 0.1;
            if (credit < 0) {                           // used up: give the time back to Windows
                throttled++;
                DWORD ms = (DWORD)(-credit * 1000 * 100 / cpu_max) + 1;
                Sleep(ms > 50 ? 50 : ms);
                break;                                  // the waveOut event wakes us again
            }
            eng.render(sh, vxd_now(), g_buf[i].data(), g_blk);
            QueryPerformanceCounter(&t1);
            rec_write(g_buf[i].data(), g_blk);
            double used = (double)(t1.QuadPart - t0.QuadPart) / qf.QuadPart;
            credit -= used;
            double l = used * cur.rate / g_blk;
            load = load * 0.97 + l * 0.03;
            h.dwFlags &= ~WHDR_DONE;
            if (waveOutWrite(g_wo, &h, sizeof h) != MMSYSERR_NOERROR) {
                wave_close();
                last_wave_try = GetTickCount();
                break;
            }
        }
        if (g_wo && out_sleep && !g_rec && eng.can_sleep(sh)) {
            waveOutReset(g_wo);                         // every buffer comes back as done
            eng.st.out_asleep = true;
        }
        }

        // /VSCAUTO: shed the SC-55's reverb/chorus when the CPU cannot keep up
        DWORD now = GetTickCount();
        if (sh && sh->dma_active) last_dma = now;
        if (g_wo && buf_auto && sh && now - last_dma < 1000) {
            if (!dry_t || now - dry_t > 3000) { dry_t = now; dry_u = underruns; }
            else if (underruns - dry_u >= 3 && buf_ms < 300) {
                buf_ms = buf_ms < 160 ? 160 : 300;
                eng.pcm_slack_ms = buf_ms;
                logf("audio: the output ran dry while a DOS program played (Windows runs us in short turns "
                     "then) - buffer now %d ms", buf_ms);
                wave_close(); last_wave_try = 0; dry_t = 0;
                continue;
            }
        }
        if (buf_ms > buf_base && now - last_dma > 60000) {
            buf_ms = buf_base; eng.pcm_slack_ms = 0;
            logf("audio: no DOS sound for a minute - buffer back to %d ms", buf_ms);
            if (g_wo) { wave_close(); last_wave_try = 0; continue; }
        }
        if (cur.scauto && eng.midi && !eng.st.midi_idle) {
            if (!shed && load > 0.70) {
                eng.midi->set_fx(false);
                shed = true;
                shed_since = now;
            } else if (shed && load < 0.35 && now - shed_since > 10000) {
                eng.midi->set_fx(cur.scfx != 0);
                shed = false;
            }
        } else if (shed && !cur.scauto) {
            eng.midi->set_fx(cur.scfx != 0);
            shed = false;
        }
        eng.st.fx_shed = shed;
        // priority by load: "highest" only while the synth is light, so a heavy one can
        // never lock Windows up (the credit above caps the time it takes in any case)
        if (g_wo) {
            int want = load < 0.5 ? THREAD_PRIORITY_HIGHEST : load < 0.85 ? THREAD_PRIORITY_ABOVE_NORMAL
                                                                          : THREAD_PRIORITY_NORMAL;
            if (want != prio) { prio = want; SetThreadPriority(GetCurrentThread(), prio); }
        }
        if (g_wo && !step_ok_at) step_ok_at = now + 20000;
        if (step_ok_at > 1 && (int32_t)(now - step_ok_at) >= 0) { set_step("ok"); step_ok_at = 1; logf("start-up complete"); }
        if (g_sh) {                         // Windows MIDI volume changes, for the log
            static uint32_t last_vol = 0xFFFFFFFFu, last_set = 0;
            uint32_t v = g_sh->midi_volume, vs = g_sh->midi_volume_set;
            if (vs && (v != last_vol || vs != last_set))
                logf("Windows MIDI volume: left %u%%, right %u%%", (unsigned)((v & 0xFFFF) * 100 / 65535), (unsigned)((v >> 16) * 100 / 65535));
            last_vol = v; last_set = vs;
        }
        if (g_sh) {                         // TraceFile=1 in SC55W.INI: the VxD's DMA/IRQ/DSP trace into SC55W.TRC
            static int tr_on = -1; static FILE* trf; static uint32_t tr_last; static std::string tr_path; static DWORD tr_t;
            if (tr_on < 0) {
                tr_on = ini_int("TraceFile", 0);
                if (tr_on) { std::string p = g_inipath.substr(0, g_inipath.find_last_of("\\") + 1) + "SC55W.TRC"; trf = fopen(p.c_str(), "w"); if (trf) fclose(trf); trf = trf ? (FILE*)1 : NULL; tr_path = p; tr_last = g_sh->tr_head; }
            }
            if (trf && now - tr_t >= 100 && (tr_t = now, true)) {
                FILE* f = fopen(tr_path.c_str(), "a");          // closed every time: Windows only updates the size then
                uint32_t h = g_sh->tr_head;
                if (!f) h = tr_last;
                if (h - tr_last > TRACE_SIZE) { fprintf(f, "-- %u lost\n", (unsigned)(h - tr_last - TRACE_SIZE)); tr_last = h - TRACE_SIZE; }
                for (; tr_last != h; tr_last++) {
                    const uint32_t* e = (const uint32_t*)g_sh->tr[tr_last & (TRACE_SIZE - 1)];
                    fprintf(f, "%10u %2u %08X %08X\n", (unsigned)e[0], (unsigned)e[1], (unsigned)e[2], (unsigned)e[3]);
                }
                if (f) fclose(f);
            }
        }
        // the first minute after start: a trace every 5 s (a freeze then leaves its last line)
        if (now - trace_start < 60000 && now - trace_last >= 5000) {
            trace_last = now;
            logf("trace: load %d%%, priority %d, throttled %u, underruns %u, %s", (int)(load * 100), prio, throttled,
                 underruns, eng.st.midi_idle ? "synth idle" : "synth running");
        }

        {   // every 30 s, when something went wrong since the last line: a trace in SC55W.LOG
            static DWORD last_log; static unsigned lu, ll, lr;
            if (now - last_log > 30000) {
                last_log = now;
                if (underruns != lu || eng.st.late_events != ll || eng.st.resyncs != lr)
                    logf("audio: underruns %u, late events %u, resyncs %u, load %d%%%s", underruns, eng.st.late_events,
                         eng.st.resyncs, (int)(load * 100), shed ? ", reverb/chorus shed" : "");
                lu = underruns; ll = eng.st.late_events; lr = eng.st.resyncs;
            }
        }
        if (now - last_pub > 250) {
            last_pub = now;
            EnterCriticalSection(&g_cs);
            g_pub.st = eng.st;
            g_pub.underruns = underruns;
            g_pub.load = load;
            g_pub.shed = shed;
            LeaveCriticalSection(&g_cs);
        }
    }
    wave_close();
    rec_close();
    return 0;
}

static void push_options(const Options& o)
{
    EnterCriticalSection(&g_cs);
    g_pending = o;
    LeaveCriticalSection(&g_cs);
    InterlockedExchange(&g_have_pending, 1);
}

// ---------------------------------------------------------------------------
// options file
static void load_options()
{
    std::string t;
    g_opt = Options();
    g_opt.apply_blaster(getenv("BLASTER"));      // what SBEMU does in DOS too
    if (load_text(g_optpath, t)) g_opt.parse(t);
}

static bool save_options(const Options& o)
{
    std::string line = o.format();
    bool ok = save_text(g_optpath, line);
    // SBOPTS.BAT for the pack's older launchers, as SBSET writes it
    FILE* f = fopen(g_batpath.c_str(), "wb");
    if (f) {
        fprintf(f, "@ECHO OFF\r\nSET SBOPTS=%s\r\n", line.c_str());
        fclose(f);
    }
    return ok;
}

// ---------------------------------------------------------------------------
// autostart (Run key)
static const char RUN_KEY[] = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";

static bool autostart_get()
{
    HKEY k;
    bool on = false;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, RUN_KEY, 0, KEY_READ, &k) == ERROR_SUCCESS) {
        on = RegQueryValueExA(k, "SC55W", NULL, NULL, NULL, NULL) == ERROR_SUCCESS;
        RegCloseKey(k);
    }
    return on;
}

static void autostart_set(bool on)
{
    HKEY k;
    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, RUN_KEY, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL) != ERROR_SUCCESS) return;
    if (on) {
        char b[MAX_PATH];
        GetModuleFileNameA(NULL, b, sizeof b);
        std::string v = short_path(b) + " /BOOT";
        RegSetValueExA(k, "SC55W", 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)v.size() + 1);
    } else {
        RegDeleteValueA(k, "SC55W");
    }
    RegCloseKey(k);
}

// is SC55MIDI.DRV registered as a Windows MIDI device?
static bool midi_driver_installed()
{
    char key[16], val[MAX_PATH];
    for (int i = 0; i < 10; i++) {
        if (i) snprintf(key, sizeof key, "midi%d", i);
        else strcpy(key, "midi");
        GetPrivateProfileStringA("drivers", key, "", val, sizeof val, "SYSTEM.INI");
        if (strstr(_strupr(val), "SC55MIDI.DRV")) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// text helpers for the UI
static const char* type_name(int t)
{
    static const char* n[] = {"", "SB 1.5 (DSP 1.05)", "SB 2.0 (DSP 2.01)", "SB 2.0 (DSP 2.01) alt.",
                              "SB Pro (DSP 3.00)", "SB Pro 2 (DSP 3.02)", "SB16 (DSP 4.05)"};
    return t >= 1 && t <= 6 ? n[t] : "?";
}

static std::string attach_problems(const Options& o, DWORD st)
{
    std::string m;
    char b[300];
    if (st & ST_ERR_BUSY) m += "- Another program is already using SC55W.VXD.\r\n";
    if (st & (ST_ERR_LOCK | ST_ERR_BLOCK)) m += "- The driver could not set up its shared memory.\r\n";
    if (st & ST_ERR_SBPORT) {
        snprintf(b, sizeof b, "- Port %X is taken by another driver (a sound card's own Sound Blaster "
                 "emulation?). DOS programs get no Sound Blaster.\r\n", o.sb_port);
        m += b;
    }
    if (st & ST_ERR_OPLPORT) m += "- Port 388 (AdLib/FM) is taken by another driver.\r\n";
    if (st & ST_ERR_MPUPORT) {
        snprintf(b, sizeof b, "- Port %X (MPU-401) is taken by another driver; DOS MIDI will not "
                 "reach the synth.\r\n", o.mpu_port);
        m += b;
    }
    if (st & ST_ERR_IRQ) {
        snprintf(b, sizeof b, "- IRQ %d could not be taken over.\r\n", o.irq);
        m += b;
    }
    if (st & ST_ERR_DMA8) {
        snprintf(b, sizeof b, "- DMA %d could not be taken over.\r\n", o.dma8);
        m += b;
    }
    if (st & ST_ERR_DMA16) {
        snprintf(b, sizeof b, "- 16-bit DMA %d could not be taken over.\r\n", o.dma16);
        m += b;
    }
    if (st & ST_ERR_JOYPORT) m += "- The game port (201) is taken by another driver; DOS programs get no joystick.\r\n";
    if (st & ST_ERR_JBIOS) m += "- The BIOS joystick call (INT 15h) could not be hooked.\r\n";
    return m;
}

// ---------------------------------------------------------------------------
// joystick: the Windows joystick API -> the shared block -> game port / INT 15h in DOS boxes.
// Stick A = joystick 1 X/Y. Stick B = joystick 2 X/Y, or joystick 1's second stick (U or Z
// across, R down) when there is no joystick 2. Buttons 1-4 of joystick 1 = A1 A2 B1 B2;
// joystick 2's buttons 1-2 also press B1 B2. Any controller Windows knows works, e.g. an
// Xbox 360 pad with its Windows 98 driver.
static std::string g_joy_desc = "not checked yet";

static u16 joy_scale(DWORD v, UINT lo, UINT hi)
{
    if (hi <= lo) return 32768;
    if (v <= lo) return 0;
    if (v >= hi) return 65535;
    return (u16)((uint64_t)(v - lo) * 65535 / (hi - lo));
}

// Xbox 360 pads (and the third-party pads the bundled X360 driver knows) have no driver in
// Windows 98 itself. A plugged-in one without a driver gives "no joystick", so look for it in
// the Configuration Manager's list of present devices (HKEY_DYN_DATA) and point to the driver
// in WIN98\X360. Returns the pad's USB id ("VID_045E&PID_028E") or "".
#if X360_TEST                    // test builds: 86Box has no USB pads, so use the PCI host bridge
#define X360_PREFIX "PCI\\"         // (X360_TEST=2: pretend it has no driver)
#define X360_DRVVAL (X360_TEST == 2 ? "NoSuchValue" : "Driver")
#else
#define X360_PREFIX "USB\\"
#define X360_DRVVAL "Driver"
#endif
static const char* const X360_IDS[] = {
#if X360_TEST
    "VEN_8086&DEV_7190",
#endif
    "VID_045E&PID_028E", "VID_046D&PID_C242", "VID_0738&PID_4716", "VID_0738&PID_4718", "VID_0738&PID_4726",
    "VID_0738&PID_4728", "VID_0738&PID_4736", "VID_0738&PID_4738", "VID_0738&PID_A728", "VID_0738&PID_CB02",
    "VID_0F0D&PID_000A", "VID_0F0D&PID_000B", "VID_0F0D&PID_000C", "VID_0F0D&PID_000D", "VID_0F0D&PID_000F",
    "VID_0F0D&PID_0016", "VID_0F0D&PID_0031", "VID_1BAD&PID_F028", "VID_1BAD&PID_F029", "VID_1BAD&PID_F039",
    "VID_1BAD&PID_F500", "VID_1BAD&PID_F501", "VID_1BAD&PID_F502", "VID_1BAD&PID_F504", "VID_1BAD&PID_F506"};

static std::string x360_without_driver()
{
    HKEY root;
    if (RegOpenKeyExA(HKEY_DYN_DATA, "Config Manager\\Enum", 0, KEY_READ, &root) != ERROR_SUCCESS) return "";
    std::string found;
    char name[64];
    for (DWORD i = 0; found.empty(); i++) {
        DWORD nl = sizeof name;
        if (RegEnumKeyExA(root, i, name, &nl, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
        HKEY dn;
        if (RegOpenKeyExA(root, name, 0, KEY_READ, &dn) != ERROR_SUCCESS) continue;
        char hw[256] = "";
        DWORD hl = sizeof hw - 1, problem = 0, pl = sizeof problem;
        RegQueryValueExA(dn, "HardWareKey", NULL, NULL, (BYTE*)hw, &hl);
        RegQueryValueExA(dn, "Problem", NULL, NULL, (BYTE*)&problem, &pl);
        RegCloseKey(dn);
        std::string h = hw;
        for (auto& ch : h) ch = (char)toupper((unsigned char)ch);
        if (h.compare(0, strlen(X360_PREFIX), X360_PREFIX)) continue;
        for (const char* id : X360_IDS) {
            if (h.find(id) == std::string::npos) continue;
            // driver installed = the device's Enum key names one and the devnode has no problem
            HKEY ek;
            char drv[128] = "";
            DWORD dl = sizeof drv - 1;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, ("Enum\\" + std::string(hw)).c_str(), 0, KEY_READ, &ek) == ERROR_SUCCESS) {
                RegQueryValueExA(ek, X360_DRVVAL, NULL, NULL, (BYTE*)drv, &dl);
                RegCloseKey(ek);
            }
            if (!drv[0] || problem) found = id;
            break;
        }
    }
    RegCloseKey(root);
    return found;
}

static DWORD WINAPI x360_prompt(LPVOID)
{
    std::string dir = path_join(g_exedir, "X360");
    int r = MessageBoxA(NULL, (std::string(
        "An Xbox 360 controller is plugged in, but Windows 98 has no driver for it, so neither Windows "
        "nor DOS games can use it.\r\n\r\nThis pack comes with an unofficial Windows 98 driver for it "
        "(folder ") + dir + ").\r\n\r\nShow how to install it?\r\n(No = do not ask again)").c_str(),
        APP_TITLE, MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND);
    if (r == IDYES) {
        ShellExecuteA(NULL, "open", path_join(dir, "X360.TXT").c_str(), NULL, dir.c_str(), SW_SHOWNORMAL);
        ShellExecuteA(NULL, "open", dir.c_str(), NULL, NULL, SW_SHOWNORMAL);
    } else WritePrivateProfileStringA("SC55W", "X360Prompt", "0", g_inipath.c_str());
    return 0;
}

// ---------------------------------------------------------------------------
// game pad -> PadInput. SC55W.INI [Pad] says which Windows joystick button / axis is which pad
// button (defaults: the Xbox 360 driver's order: A B X Y LB RB Back Start LS RS = buttons 1-10,
// D-pad = POV hat, LT/RT = Z axis (LT up, RT down), right stick = U across, R down).
struct PadMap { int btn[PB_COUNT]; char trig, rx, ry; };
static PadMap g_padmap;
static bool g_padkeys;
static std::string g_pad_title = "Default", g_pad_prog, g_pad_last = "none yet";

static void pad_map_load()
{
    static const int def[PB_COUNT] = {1, 2, 3, 4, 5, 6, 0, 0, 9, 10, 7, 8, 0, 0, 0, 0};
    for (int i = 0; i < PB_COUNT; i++)
        g_padmap.btn[i] = GetPrivateProfileIntA("Pad", pad_button_name(i), def[i], g_inipath.c_str());
    char b[8];
    GetPrivateProfileStringA("Pad", "Triggers", "Z", b, sizeof b, g_inipath.c_str());
    g_padmap.trig = (char)toupper((unsigned char)b[0]);
    GetPrivateProfileStringA("Pad", "RStickX", "U", b, sizeof b, g_inipath.c_str());
    g_padmap.rx = (char)toupper((unsigned char)b[0]);
    GetPrivateProfileStringA("Pad", "RStickY", "R", b, sizeof b, g_inipath.c_str());
    g_padmap.ry = (char)toupper((unsigned char)b[0]);
}

static int axis_val(const JOYCAPSA& c, const JOYINFOEX& j, char a)
{
    DWORD v; UINT lo, hi; DWORD has;
    switch (a) {
    case 'X': v = j.dwXpos; lo = c.wXmin; hi = c.wXmax; has = 1; break;
    case 'Y': v = j.dwYpos; lo = c.wYmin; hi = c.wYmax; has = 1; break;
    case 'Z': v = j.dwZpos; lo = c.wZmin; hi = c.wZmax; has = c.wCaps & JOYCAPS_HASZ; break;
    case 'R': v = j.dwRpos; lo = c.wRmin; hi = c.wRmax; has = c.wCaps & JOYCAPS_HASR; break;
    case 'U': v = j.dwUpos; lo = c.wUmin; hi = c.wUmax; has = c.wCaps & JOYCAPS_HASU; break;
    case 'V': v = j.dwVpos; lo = c.wVmin; hi = c.wVmax; has = c.wCaps & JOYCAPS_HASV; break;
    default: return 0;
    }
    if (!has) return 0;
    return (int)joy_scale(v, lo, hi) - 32768;
}

static void pad_read(const JOYCAPSA& c, const JOYINFOEX& j, PadInput& in)
{
    in.present = true;
    for (int i = 0; i < PB_COUNT; i++) {
        int n = g_padmap.btn[i];
        if (n >= 1 && n <= 32) in.button[i] = (j.dwButtons >> (n - 1)) & 1;
    }
    if ((c.wCaps & JOYCAPS_HASPOV) && j.dwPOV <= 35900) {            // D-pad
        DWORD p = j.dwPOV;
        in.button[PB_DUP] |= p >= 31500 || p <= 4500;
        in.button[PB_DRIGHT] |= p >= 4500 && p <= 13500;
        in.button[PB_DDOWN] |= p >= 13500 && p <= 22500;
        in.button[PB_DLEFT] |= p >= 22500 && p <= 31500;
    }
    if (g_padmap.trig && strchr("XYZRUV", g_padmap.trig)) {          // triggers on one axis
        int t = axis_val(c, j, g_padmap.trig);
        in.trig = t;
        if (t > 12000) in.button[PB_LT] = true;
        if (t < -12000) in.button[PB_RT] = true;
    }
    in.axis[PA_LX] = axis_val(c, j, 'X');
    in.axis[PA_LY] = axis_val(c, j, 'Y');
    in.axis[PA_RX] = axis_val(c, j, g_padmap.rx);
    in.axis[PA_RY] = axis_val(c, j, g_padmap.ry);
}

// the last pad button pressed, for the status window (to check [Pad] and PADUSER.TXT)
static void note_input(const PadInput& in, DWORD raw)
{
    static bool prev[PB_COUNT];
    static DWORD prev_raw;
    for (int i = 0; i < PB_COUNT; i++) {
        if (in.button[i] && !prev[i]) {
            char b[80];
            DWORD nb = raw & ~prev_raw;
            int n = g_padmap.btn[i] >= 1 && g_padmap.btn[i] <= 32 && (raw >> (g_padmap.btn[i] - 1) & 1) ? g_padmap.btn[i] : 0;
            for (int k = 0; k < 32 && !n; k++) if (nb >> k & 1) n = k + 1;
            if (n) snprintf(b, sizeof b, "%s (Windows button %d)", pad_button_name(i), n);
            else snprintf(b, sizeof b, "%s", pad_button_name(i));
            EnterCriticalSection(&g_cs);
            g_pad_last = b;
            LeaveCriticalSection(&g_cs);
        }
        prev[i] = in.button[i];
    }
    prev_raw = raw;
}

// PADMAP.TXT from the user's keyb2joypad.cpp (in <pack>\PADMAP), when it is new
static void pad_files_load()
{
    std::string cpp = path_join(path_join(g_base, "PADMAP"), "keyb2joypad.cpp");
    std::string txt = path_join(g_base, "PADMAP.TXT"), tmp = path_join(g_base, "PADMAP.TMP");
    WIN32_FILE_ATTRIBUTE_DATA a, b;
    if (GetFileAttributesExA(cpp.c_str(), GetFileExInfoStandard, &a) &&
        (!GetFileAttributesExA(txt.c_str(), GetFileExInfoStandard, &b) ||
         CompareFileTime(&a.ftLastWriteTime, &b.ftLastWriteTime) > 0)) {
        char msg[200];
        if (padconv_run(cpp.c_str(), tmp.c_str(), msg, sizeof msg) == 0) {
            DeleteFileA(txt.c_str());
            MoveFileA(tmp.c_str(), txt.c_str());
            logf("pad: PADMAP.TXT made from %s: %s", cpp.c_str(), msg);
        } else {
            DeleteFileA(tmp.c_str());
            logf("pad: could not read %s: %s", cpp.c_str(), msg);
        }
    }
    std::string log;
    pad_load(txt, path_join(g_base, "PADUSER.TXT"), log);
    logf("pad: %s", log.c_str());
}

// a NUKED joystick (NUKEDJOY.VXD) shows what we feed it: never read it back as a pad
static JoyOutSettings g_joyout;
static volatile LONG g_joyout_dirty;     // the settings window saved [Joystick]
static PadInput g_padin[2];
static DWORD g_rawbtn;                   // Windows buttons of the first pad (settings: learn)
static std::string g_padname[2];
static bool is_nuked_joystick(UINT j, const JOYCAPSA& c)
{
    std::string v = c.szOEMVxD;
    for (auto& ch : v) ch = (char)toupper((unsigned char)ch);
    if (v.find("NUKEDJOY") != std::string::npos) return true;
    if (!strncmp(c.szPname, "NUKED", 5)) return true;
    if (g_sh) for (int k = 0; k < SC55W_NWJOY; k++) if (g_sh->wjoy[k].joyid == j + 1) return true;
    return false;
}

static DWORD WINAPI joy_main(LPVOID)
{
    bool x360_asked = false, focus_is_dos = false;
    DWORD last_focus = 0;
    u32 focus_vm = 0, focus_serial = 0xFFFFFFFFUL;
    JOYCAPSA cap[2];
    UINT jid[2] = {0, 1};
    bool ok[2] = {false, false};
    DWORD last_ini = 0;
    DWORD last_check = 0;
    DWORD busy_until = GetTickCount() + 2000;
    bool fast = false;                       // 1 ms timer on (timeBeginPeriod) while busy
    std::string last_desc;
    while (!g_quit) {
        DWORD now = GetTickCount();
        if (!last_check || now - last_check > 3000) {       // plugged in / out
            last_check = now;
            std::string d;
            // the first two Windows joysticks that are real pads (not our own NUKED ones,
            // which only show what we feed them)
            ok[0] = ok[1] = false;
            UINT nj = joyGetNumDevs();
            if (nj > 16) nj = 16;
            int found = 0;
            for (UINT j = 0; j < nj && found < 2; j++) {
                JOYCAPSA c;
                if (joyGetDevCapsA(j, &c, sizeof c) != JOYERR_NOERROR) continue;
                if (is_nuked_joystick(j, c)) {
                    if (joyout_fix_calibration(j)) logf("joystick: NUKED joystick %u calibrated", j + 1);
                    continue;
                }
                JOYINFOEX t; t.dwSize = sizeof t; t.dwFlags = JOY_RETURNALL;
                if (joyGetPosEx(j, &t) != JOYERR_NOERROR) continue;     // configured but unplugged -> no
                cap[found] = c; jid[found] = j; ok[found] = true;
                char b[160];
                snprintf(b, sizeof b, "%s%u: %s (%u axes, %u buttons)", d.empty() ? "" : "; ", j + 1,
                         c.szPname, c.wNumAxes, c.wNumButtons);
                d += b;
                found++;
            }
            if (d.empty()) {
                std::string x = x360_without_driver();
                if (!x.empty()) {
                    d = "none - an Xbox 360 controller (" + x + ") is plugged in without a driver: "
                        "install it from WIN98\\X360 (X360.TXT)";
                    if (!x360_asked && ini_int("X360Prompt", 1)) {
                        x360_asked = true;
                        DWORD tid;       // Windows 98: CreateThread fails with a NULL thread id
                        HANDLE t = CreateThread(NULL, 0, x360_prompt, NULL, 0, &tid);
                        if (t) CloseHandle(t);
                    }
                } else d = "none found in Windows (Control Panel > Game Controllers)";
            }
            if (d != last_desc) {
                logf("joystick: %s", d.c_str());
                last_desc = d;
                EnterCriticalSection(&g_cs);
                g_joy_desc = d;
                LeaveCriticalSection(&g_cs);
            }
        }
        if (g_sh) {
            JOYINFOEX ji[2];
            bool got[2] = {false, false};
            for (int j = 0; j < 2; j++) {
                if (!ok[j]) continue;
                ji[j].dwSize = sizeof ji[j];
                ji[j].dwFlags = JOY_RETURNALL;
                got[j] = joyGetPosEx(jid[j], &ji[j]) == JOYERR_NOERROR;
            }
            if (!last_ini || InterlockedExchange(&g_joyout_dirty, 0)) {   // settings saved: reload
                last_ini = now;
                JoyOutSettings js;
                joyout_load(g_inipath, js);
                EnterCriticalSection(&g_cs);
                g_joyout = js;
                LeaveCriticalSection(&g_cs);
            }
            PadInput pin[2];
            if (got[0]) pad_read(cap[0], ji[0], pin[0]);
            if (got[1]) pad_read(cap[1], ji[1], pin[1]);
            PadInput& in = pin[0];
            if (in.present) note_input(in, ji[0].dwButtons);
            EnterCriticalSection(&g_cs);
            g_padin[0] = pin[0]; g_padin[1] = pin[1];
            g_rawbtn = got[0] ? ji[0].dwButtons : 0;
            g_padname[0] = ok[0] ? cap[0].szPname : ""; g_padname[1] = ok[1] ? cap[1].szPname : "";
            LeaveCriticalSection(&g_cs);

            // which game runs in the DOS box that has the keyboard (10 times a second)
            bool focus_dos = false;
            if (g_padkeys) {
                if (now - last_focus >= 100) {
                    last_focus = now;
                    SC55W_FOCUS f;
                    DWORD got_n = 0;
                    if (DeviceIoControl(g_vxd, IOCTL_SC55W_FOCUS, NULL, 0, &f, sizeof f, &got_n, NULL) &&
                        got_n == sizeof f) {
                        focus_is_dos = f.is_dos != 0;
                        if (f.is_dos && (f.vm != focus_vm || f.serial != focus_serial)) {
                            focus_vm = f.vm; focus_serial = f.serial;
                            std::vector<std::pair<std::string, uint32_t>> progs;
                            for (u32 i = 0; i < f.depth && i < SC55W_NPROG; i++) {
                                f.prog[i].path[127] = 0;
                                progs.push_back({f.prog[i].path, f.prog[i].size});
                            }
                            std::string where, t = pad_select(progs, where);
                            std::string prog = progs.empty() ? std::string("no program") : progs.back().first;
                            if (t != g_pad_title || prog != g_pad_prog) {
                                logf("pad: layout \"%s\" for %s%s%s", t.c_str(), prog.c_str(),
                                     where.empty() ? "" : ", found by ", where.c_str());
                                EnterCriticalSection(&g_cs);
                                g_pad_title = t; g_pad_prog = prog;
                                LeaveCriticalSection(&g_cs);
                            }
                        }
                    }
                }
                focus_dos = focus_is_dos;
            }
            PadOutput out;
            pad_step(in, focus_dos, out);
            if (!out.scancodes.empty()) {
                DWORD r = 0, got_n = 0;
                DeviceIoControl(g_vxd, IOCTL_SC55W_KEYS, out.scancodes.data(), (DWORD)out.scancodes.size(),
                                &r, 4, &got_n, NULL);
            }
            // a second Windows joystick still gives the DOS stick B and buttons 3-4
            if (got[1]) {
                const JOYCAPSA& c = cap[1];
                out.joy_axis[2] = joy_scale(ji[1].dwXpos, c.wXmin, c.wXmax);
                out.joy_axis[3] = joy_scale(ji[1].dwYpos, c.wYmin, c.wYmax);
                out.joy_present |= 2;
                out.joy_buttons |= (u8)((ji[1].dwButtons & 3) << 2);
            }
            JoyOutSettings js;
            EnterCriticalSection(&g_cs);
            js = g_joyout;
            LeaveCriticalSection(&g_cs);
            joyout_port(js, pin, out, g_sh);
            for (int i = 0; i < 4; i++) g_sh->joy_axis[i] = out.joy_axis[i];
            g_sh->joy_buttons = out.joy_buttons;
            g_sh->joy_present = out.joy_present;
            joyout_windows(js, pin, out, g_sh);

            // CPU: read the pads 100 times a second only while something uses them (a DOS
            // program reads the game port or INT 15h, a Windows game the NUKED joystick, the pad
            // types keys into the DOS box that has the keyboard, or the settings window is open);
            // otherwise 10 times a second, which still notices a pad being picked up. Reading an
            // analog game-port joystick through Windows busy-waits ~1 ms each time.
            static u32 last_reads, last_polls;
            u32 reads = g_sh->joy_reads, polls = g_sh->wjoy[0].polls + g_sh->wjoy[1].polls;
            if (reads != last_reads || polls != last_polls || focus_dos || g_settings_dlg ||
                !out.scancodes.empty())
                busy_until = now + 2000;
            last_reads = reads; last_polls = polls;
        }
        bool busy = (int32_t)(busy_until - now) > 0;
        if (busy != fast) {                      // 10 ms sleeps need the 1 ms timer (Windows 98: ~55 ms without)
            if (busy) timeBeginPeriod(1); else timeEndPeriod(1);
            fast = busy;
        }
        Sleep(busy ? 10 : 100);
    }
    if (fast) timeEndPeriod(1);
    return 0;
}

static std::string status_text()
{
    EnterCriticalSection(&g_cs);
    Published p = g_pub;
    LeaveCriticalSection(&g_cs);
    const Options& o = g_opt;
    std::string s;
    char b[400];
    snprintf(b, sizeof b, "%s - %s\r\n\r\n", APP_TITLE, p.state.c_str());
    s += b;
    if (g_vxd == INVALID_HANDLE_VALUE) {
        s += "DOS sound is off: " + g_vxd_error + "\r\n";
    } else {
        snprintf(b, sizeof b, "DOS boxes: %s at %X, IRQ %d, DMA %d", type_name(o.sbtype), o.sb_port, o.irq, o.dma8);
        s += b;
        if (o.sbtype >= 6) { snprintf(b, sizeof b, "/%d", o.dma16); s += b; }
        s += "\r\n";
        snprintf(b, sizeof b, "   FM: %s.  MIDI: %s\r\n",
                 !o.opl ? "off" : o.fmcore ? "Nuked OPL3 at 388" : "DOSBox OPL at 388",
                 !o.vmpu ? "MPU-401 off" : (o.mpu_port == 0x300 ? "MPU-401 at 300" : "MPU-401 at 330"));
        s += b;
        std::string pr = attach_problems(o, p.attach);
        if (!pr.empty()) s += pr;
    }
    if (g_vxd != INVALID_HANDLE_VALUE) {
        EnterCriticalSection(&g_cs);
        std::string jd = g_joy_desc;
        LeaveCriticalSection(&g_cs);
        if (p.attach & ST_OK_JOY) {
            snprintf(b, sizeof b, "   Joystick: game port 201%s, %lu reads. Windows joystick %s\r\n",
                     (p.attach & ST_OK_JBIOS) ? " + BIOS" : "", g_sh ? (unsigned long)g_sh->joy_reads : 0UL, jd.c_str());
            s += b;
        } else if (!(p.attach & ST_ERR_JOYPORT)) s += "   Joystick: off (SC55W.INI Joystick=0)\r\n";
        if (g_padkeys) {
            EnterCriticalSection(&g_cs);
            std::string t = g_pad_title, pr = g_pad_prog, la = g_pad_last;
            LeaveCriticalSection(&g_cs);
            s += "   Pad keys: layout \"" + t + "\"" + (pr.empty() ? std::string() : " (" + pr + ")") +
                 ". Last button: " + la + "\r\n";
        }
    }
    s += "MIDI synth: " + (p.synth_error.empty() ? p.synth : "none - " + p.synth_error) + "\r\n";
    if (p.st.midi_volume != 100) {
        snprintf(b, sizeof b, "Windows MIDI volume: %d%%\r\n", p.st.midi_volume);
        s += b;
    }
    s += midi_driver_installed() ? "Windows MIDI device: \"NUKED SB-EMU\" (installed)\r\n"
                                 : "Windows MIDI device: not installed (run SETUP98)\r\n";
    if (!p.wave_error.empty()) s += p.wave_error + "\r\n";
    else {
        snprintf(b, sizeof b, "Output: %d Hz, %d ms buffer, CPU %d%%%s%s\r\n", p.rate, p.buf_ms,
                 (int)(p.load * 100 + 0.5), p.shed ? " (reverb/chorus paused)" : "",
                 p.st.out_asleep ? ", paused while silent" : p.st.midi_idle ? ", synth idle" : "");
        s += b;
    }
    snprintf(b, sizeof b, "\r\nActivity: MIDI %u bytes, FM %u writes, digital %u frames\r\n",
             p.st.midi_bytes, p.st.opl_writes, p.st.pcm_frames);
    s += b;
    if (g_sh && p.attached) {
        snprintf(b, sizeof b, "Sound Blaster: %lu DSP commands, %lu interrupts, %lu DMA bytes\r\n",
                 (unsigned long)g_sh->dsp_cmds, (unsigned long)g_sh->irq_count, (unsigned long)g_sh->dma_bytes);
        s += b;
    }
    snprintf(b, sizeof b, "Late events %u, resyncs %u, output underruns %u", p.st.late_events, p.st.resyncs, p.underruns);
    s += b;
    if (g_sh && (g_sh->ev_overflow || g_sh->pcm_overflow)) {
        snprintf(b, sizeof b, ", lost %lu/%lu", (unsigned long)g_sh->ev_overflow, (unsigned long)g_sh->pcm_overflow);
        s += b;
    }
    s += "\r\n";
    return s;
}

// ---------------------------------------------------------------------------
// tray
static NOTIFYICONDATAA g_nid;
static UINT g_taskbar_created;

static void tray_add()
{
    memset(&g_nid, 0, sizeof g_nid);
    g_nid.cbSize = NOTIFYICONDATAA_V1_SIZE;
    g_nid.hWnd = g_wnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = (HICON)LoadImageA(g_inst, MAKEINTRESOURCEA(IDI_APP), IMAGE_ICON, 16, 16, 0);
    lstrcpynA(g_nid.szTip, "NUKED SB-EMU", sizeof g_nid.szTip);
    Shell_NotifyIconA(NIM_ADD, &g_nid);
}

static void tray_tip()
{
    EnterCriticalSection(&g_cs);
    std::string st = g_pub.state, syn = g_pub.synth;
    LeaveCriticalSection(&g_cs);
    std::string t = "NUKED SB-EMU: " + st;
    if (st == "Running" && !syn.empty()) t = "NUKED SB-EMU: " + syn;
    lstrcpynA(g_nid.szTip, t.c_str(), 64);
    g_nid.uFlags = NIF_TIP;
    Shell_NotifyIconA(NIM_MODIFY, &g_nid);
}

// ---------------------------------------------------------------------------
// settings dialog
static Options g_dlg_opt;          // what the dialog shows
static bool g_dlg_filling;

static void cb_add(HWND d, int id, const char* text, int data)
{
    int i = (int)SendDlgItemMessageA(d, id, CB_ADDSTRING, 0, (LPARAM)text);
    SendDlgItemMessageA(d, id, CB_SETITEMDATA, i, data);
}
static void cb_select(HWND d, int id, int data)
{
    int n = (int)SendDlgItemMessageA(d, id, CB_GETCOUNT, 0, 0);
    for (int i = 0; i < n; i++)
        if ((int)SendDlgItemMessageA(d, id, CB_GETITEMDATA, i, 0) == data) {
            SendDlgItemMessageA(d, id, CB_SETCURSEL, i, 0);
            return;
        }
    char b[16];
    snprintf(b, sizeof b, "%d", data);        // keep a value from the file even if not in the list
    cb_add(d, id, b, data);
    cb_select(d, id, data);
}
static int cb_get(HWND d, int id, int def)
{
    int i = (int)SendDlgItemMessageA(d, id, CB_GETCURSEL, 0, 0);
    return i < 0 ? def : (int)SendDlgItemMessageA(d, id, CB_GETITEMDATA, i, 0);
}
static void set_int(HWND d, int id, int v) { SetDlgItemInt(d, id, v, FALSE); }
static int get_int(HWND d, int id, int lo, int hi, int def)
{
    BOOL ok;
    int v = (int)GetDlgItemInt(d, id, &ok, FALSE);
    if (!ok) return def;
    return v < lo ? lo : v > hi ? hi : v;
}
static void check(HWND d, int id, bool on) { CheckDlgButton(d, id, on ? BST_CHECKED : BST_UNCHECKED); }
static bool checked(HWND d, int id) { return IsDlgButtonChecked(d, id) == BST_CHECKED; }

enum { SYN_SC55 = 0, SYN_SF2 = 1, SYN_MT_AUTO = 2, SYN_MT32 = 3, SYN_CM32L = 4 };
static std::vector<std::string> g_romsets;

static void settings_fill_lists(HWND d)
{
    cb_add(d, IDC_PORT, "220", 0x220);
    cb_add(d, IDC_PORT, "240", 0x240);
    const int irqs[] = {5, 7, 9};                 // what SBEMU offers (BLASTER holds one digit)
    for (int v : irqs) { char b[8]; snprintf(b, sizeof b, "%d", v); cb_add(d, IDC_IRQ, b, v); }
    const int d8[] = {0, 1, 3};
    for (int v : d8) { char b[8]; snprintf(b, sizeof b, "%d", v); cb_add(d, IDC_DMA, b, v); }
    const int d16[] = {5, 6, 7};
    for (int v : d16) { char b[8]; snprintf(b, sizeof b, "%d", v); cb_add(d, IDC_HDMA, b, v); }
    for (int t = 1; t <= 6; t++) cb_add(d, IDC_TYPE, type_name(t), t);
    cb_add(d, IDC_FM, "Off", 0);
    cb_add(d, IDC_FM, "DOSBox (fast)", 1);
    cb_add(d, IDC_FM, "Nuked (accurate)", 2);
    cb_add(d, IDC_MPU, "330", 0x330);
    cb_add(d, IDC_MPU, "300", 0x300);
    cb_add(d, IDC_MPU, "Off", 0);
    cb_add(d, IDC_SYNTH, "Roland SC-55", SYN_SC55);
    cb_add(d, IDC_SYNTH, "SoundFont", SYN_SF2);
    cb_add(d, IDC_SYNTH, "MT-32 (auto)", SYN_MT_AUTO);
    cb_add(d, IDC_SYNTH, "Roland MT-32", SYN_MT32);
    cb_add(d, IDC_SYNTH, "Roland CM-32L", SYN_CM32L);
    cb_add(d, IDC_SCVOICES, "Off", 0);
    for (int v = 4; v <= 24; v++) { char b[8]; snprintf(b, sizeof b, "%d", v); cb_add(d, IDC_SCVOICES, b, v); }
    const int rates[] = {22050, 32000, 44100, 48000};
    for (int v : rates) { char b[16]; snprintf(b, sizeof b, "%d Hz", v); cb_add(d, IDC_RATE, b, v); }
    // let the drop-down lists be wider than their boxes
    const int wide[] = {IDC_TYPE, IDC_SYNTH, IDC_ROMSET, IDC_FM};
    for (int id : wide) SendDlgItemMessageA(d, id, CB_SETDROPPEDWIDTH, 150, 0);

    // ROM sets found in the ROMS folder
    SendDlgItemMessageA(d, IDC_ROMSET, CB_ADDSTRING, 0, (LPARAM)"auto");
    std::string dir = resolve(g_dlg_opt.scrom);
    std::vector<RomFile> files = scan_roms(dir);
    Sc55Choice ch;
    std::string msg;
    g_romsets.clear();
    find_sc55(dir, files, "auto", ch, g_romsets, msg);
    for (auto& n : g_romsets) SendDlgItemMessageA(d, IDC_ROMSET, CB_ADDSTRING, 0, (LPARAM)n.c_str());
}

static void settings_show(HWND d, const Options& o)
{
    g_dlg_filling = true;
    cb_select(d, IDC_PORT, o.sb_port);
    cb_select(d, IDC_IRQ, o.irq);
    cb_select(d, IDC_DMA, o.dma8);
    cb_select(d, IDC_HDMA, o.dma16);
    cb_select(d, IDC_TYPE, o.sbtype);
    cb_select(d, IDC_FM, !o.opl ? 0 : o.fmcore ? 2 : 1);
    cb_select(d, IDC_MPU, o.vmpu ? o.mpu_port : 0);
    int syn = SYN_SC55;
    if (o.synth() == Options::SYN_SF2) syn = SYN_SF2;
    else if (o.synth() == Options::SYN_MT32) syn = o.mt32 == "MT32" ? SYN_MT32 : o.mt32 == "CM32L" ? SYN_CM32L : SYN_MT_AUTO;
    cb_select(d, IDC_SYNTH, syn);
    int r = (int)SendDlgItemMessageA(d, IDC_ROMSET, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)o.scset.c_str());
    if (r < 0) r = (int)SendDlgItemMessageA(d, IDC_ROMSET, CB_ADDSTRING, 0, (LPARAM)o.scset.c_str());
    SendDlgItemMessageA(d, IDC_ROMSET, CB_SETCURSEL, r, 0);
    SetDlgItemTextA(d, IDC_SF, o.sf.c_str());
    set_int(d, IDC_SCGAIN, o.scgain);
    cb_select(d, IDC_SCVOICES, o.scvoices);
    check(d, IDC_SCFX, o.scfx != 0);
    check(d, IDC_SCAUTO, o.scauto != 0);
    set_int(d, IDC_SFREV, o.sfrev);
    set_int(d, IDC_SFROOM, o.sfroom);
    set_int(d, IDC_SFDAMP, o.sfdamp);
    set_int(d, IDC_SFCHO, o.sfcho);
    set_int(d, IDC_SFCRATE, o.sfcrate);
    set_int(d, IDC_SFCDEPTH, o.sfcdepth);
    set_int(d, IDC_SFVOICES, o.sfvoices);
    check(d, IDC_SFSEND, o.sfsend != 0);
    set_int(d, IDC_VOL, o.vol);
    set_int(d, IDC_SBVOL, o.sbvol);
    set_int(d, IDC_OPLVOL, o.oplvol);
    set_int(d, IDC_MIDIVOL, o.midivol);
    cb_select(d, IDC_RATE, o.rate);
    set_int(d, IDC_AHEAD, o.scahead);
    check(d, IDC_FIXTC, o.fixtc != 0);
    g_dlg_filling = false;
}

static Options settings_read(HWND d)
{
    Options o = g_dlg_opt;            // keeps DOS-only switches (passthru, /VTIMER, /VSCROM)
    o.sb_port = cb_get(d, IDC_PORT, 0x220);
    o.irq = cb_get(d, IDC_IRQ, 7);
    o.dma8 = cb_get(d, IDC_DMA, 1);
    o.dma16 = cb_get(d, IDC_HDMA, 5);
    o.sbtype = cb_get(d, IDC_TYPE, 5);
    int fm = cb_get(d, IDC_FM, 1);
    o.opl = fm != 0;
    if (fm) o.fmcore = fm == 2;
    int mpu = cb_get(d, IDC_MPU, 0x330);
    o.vmpu = mpu != 0;
    if (mpu) o.mpu_port = mpu;
    char b[MAX_PATH];
    int r = (int)SendDlgItemMessageA(d, IDC_ROMSET, CB_GETCURSEL, 0, 0);
    if (r >= 0 && SendDlgItemMessageA(d, IDC_ROMSET, CB_GETLBTEXTLEN, r, 0) < (LRESULT)sizeof b) {
        SendDlgItemMessageA(d, IDC_ROMSET, CB_GETLBTEXT, r, (LPARAM)b);
        o.scset = b;
    }
    GetDlgItemTextA(d, IDC_SF, b, sizeof b);
    std::string sf = b;
    int syn = cb_get(d, IDC_SYNTH, SYN_SC55);
    o.sf.clear();
    o.mt32.clear();
    if (syn == SYN_SF2) o.sf = sf.empty() ? std::string("SF2\\GM.SF2") : sf;
    else if (syn == SYN_MT_AUTO) o.mt32 = "AUTO";
    else if (syn == SYN_MT32) o.mt32 = "MT32";
    else if (syn == SYN_CM32L) o.mt32 = "CM32L";
    o.scgain = get_int(d, IDC_SCGAIN, 0, 1600, o.scgain);
    o.scvoices = cb_get(d, IDC_SCVOICES, 0);
    o.scfx = checked(d, IDC_SCFX);
    o.scauto = checked(d, IDC_SCAUTO);
    o.sfrev = get_int(d, IDC_SFREV, 0, 100, o.sfrev);
    o.sfroom = get_int(d, IDC_SFROOM, 0, 100, o.sfroom);
    o.sfdamp = get_int(d, IDC_SFDAMP, 0, 100, o.sfdamp);
    o.sfcho = get_int(d, IDC_SFCHO, 0, 100, o.sfcho);
    o.sfcrate = get_int(d, IDC_SFCRATE, 1, 30, o.sfcrate);
    o.sfcdepth = get_int(d, IDC_SFCDEPTH, 0, 100, o.sfcdepth);
    o.sfvoices = get_int(d, IDC_SFVOICES, 8, 128, o.sfvoices);
    o.sfsend = checked(d, IDC_SFSEND);
    o.vol = get_int(d, IDC_VOL, 0, 100, o.vol);
    o.sbvol = get_int(d, IDC_SBVOL, 0, 100, o.sbvol);
    o.oplvol = get_int(d, IDC_OPLVOL, 0, 100, o.oplvol);
    o.midivol = get_int(d, IDC_MIDIVOL, 0, 400, o.midivol);
    o.rate = cb_get(d, IDC_RATE, 22050);
    o.scahead = get_int(d, IDC_AHEAD, 0, 500, o.scahead);
    o.fixtc = checked(d, IDC_FIXTC);
    return o;
}

static void settings_update(HWND d)
{
    Options o = settings_read(d);
    int syn = cb_get(d, IDC_SYNTH, SYN_SC55);
    bool sc = syn == SYN_SC55, sf = syn == SYN_SF2;
    EnableWindow(GetDlgItem(d, IDC_HDMA), o.sbtype >= 6);
    EnableWindow(GetDlgItem(d, IDC_ROMSET), sc);
    EnableWindow(GetDlgItem(d, IDC_SF), sf);
    EnableWindow(GetDlgItem(d, IDC_SFBROWSE), sf);
    EnableWindow(GetDlgItem(d, IDC_SCGAIN), !sf);
    EnableWindow(GetDlgItem(d, IDC_SCVOICES), sc);
    EnableWindow(GetDlgItem(d, IDC_SCFX), sc);
    EnableWindow(GetDlgItem(d, IDC_SCAUTO), sc);
    const int sfids[] = {IDC_SFREV, IDC_SFROOM, IDC_SFDAMP, IDC_SFCHO, IDC_SFCRATE, IDC_SFCDEPTH, IDC_SFVOICES, IDC_SFSEND};
    for (int id : sfids) EnableWindow(GetDlgItem(d, id), sf);
    std::string t = "Saved as: " + (o.format().empty() ? std::string("(all defaults)") : o.format());
    if (sc && o.rate >= 44100) t += "\r\nNote: the SC-55 at 44.1/48 kHz is heavy for a laptop CPU.";
    if (o.scahead == 0) t += "\r\nNote: render-ahead 0 is for DOS only; Windows uses 20 ms.";
    SetDlgItemTextA(d, IDC_PREVIEW, t.c_str());
}

static HWND g_page[3];
static bool joy_page_save(HWND p, std::string& note);
static void pad_page_save(HWND p);
static void settings_apply(HWND d, bool save)
{
    Options o = settings_read(d);
    g_dlg_opt = o;
    g_opt = o;
    std::string note;
    if (g_page[1]) joy_page_save(g_page[1], note);   // game port / NUKED joystick (SC55W.INI)
    if (g_page[2]) pad_page_save(g_page[2]);         // pad keys and the pad's button map
    push_options(o);                                 // re-attaches when the game port changed
    if (!note.empty()) MessageBoxA(d, note.c_str(), APP_TITLE, MB_ICONINFORMATION);
    if (save) {
        if (!save_options(o))
            MessageBoxA(d, ("Could not write " + g_optpath).c_str(), APP_TITLE, MB_ICONEXCLAMATION);
        autostart_set(checked(d, IDC_AUTOSTART));
        if (autoexec_update_blaster(o))
            MessageBoxA(d, ("AUTOEXEC.BAT now has SET BLASTER=" + blaster_for(o) +
                            ".\r\nDOS boxes see it after Windows is restarted; the Sound Blaster itself "
                            "uses the new settings right away.").c_str(), APP_TITLE, MB_ICONINFORMATION);
    }
}


// ---------------------------------------------------------------------------
// settings: tabs. Page 0 "Sound and MIDI" = the controls of the main dialog itself;
// pages 1-2 are child dialogs (IDD_PAGE_JOY, IDD_PAGE_PAD). Their settings live in SC55W.INI.
static int g_tab;
static const int g_tab_always[] = {IDC_TABS, IDC_PREVIEW, IDC_DEFAULTS, IDOK, IDC_APPLY, IDCANCEL};

static BOOL CALLBACK tab_show_ctl(HWND c, LPARAM l)
{
    if (GetParent(c) != (HWND)l) return TRUE;                 // only the dialog's own controls
    int id = GetDlgCtrlID(c);
    for (int a : g_tab_always) if (id == a) return TRUE;
    for (int i = 1; i < 3; i++) if (c == g_page[i]) return TRUE;
    ShowWindow(c, g_tab == 0 ? SW_SHOW : SW_HIDE);
    return TRUE;
}

static void tab_show(HWND d)
{
    g_tab = (int)SendDlgItemMessageA(d, IDC_TABS, TCM_GETCURSEL, 0, 0);
    EnumChildWindows(d, tab_show_ctl, (LPARAM)d);
    for (int i = 1; i < 3; i++) if (g_page[i]) ShowWindow(g_page[i], g_tab == i ? SW_SHOW : SW_HIDE);
}

static int ini_get(const char* sec, const char* key, int def) { return GetPrivateProfileIntA(sec, key, def, g_inipath.c_str()); }
static void ini_put(const char* sec, const char* key, int v)
{
    char b[16];
    snprintf(b, sizeof b, "%d", v);
    WritePrivateProfileStringA(sec, key, b, g_inipath.c_str());
}
static unsigned ini_port()
{
    char pb[16];
    GetPrivateProfileStringA("Joystick", "Port", "201", pb, sizeof pb, g_inipath.c_str());
    unsigned long v = strtoul(pb, NULL, 16);
    return v >= 0x200 && v <= 0x20F ? (unsigned)v : 0x201;
}

// --- Joystick page
static void joy_page_status(HWND p)
{
    GamePortState g = gameport_state();
    std::string t;
    char b[160];
    if (g.joystick) {
        snprintf(b, sizeof b, "Windows joystick %d is the NUKED SB-EMU Joystick", g.joystick);
        t = b;
        u32 polls = g_sh ? g_sh->wjoy[0].polls : 0;
        if (polls) { snprintf(b, sizeof b, " (%lu reads).", (unsigned long)polls); t += b; }
        else t += " (no game has read it yet).";
    } else t = "Not set up: Set up... adds it to Game Controllers.";
    if (g.device) { snprintf(b, sizeof b, "\r\nNUKED game port in Device Manager at %X.", g.port); t += b; }
    else t += "\r\nNo NUKED game port in Device Manager.";
    SetDlgItemTextA(p, IDC_J_NSTAT, t.c_str());
    EnableWindow(GetDlgItem(p, IDC_J_REMOVE), g.device || g.joystick || g.type);
}

static void joy_page_load(HWND p)
{
    static const char* types[] = {"Standard (2 sticks, 4 buttons)", "CH Flightstick Pro (hat)", "Thrustmaster FCS (hat)",
                                  "Gravis GamePad Pro (GrIP)"};
    if (!SendDlgItemMessageA(p, IDC_J_ADDR, CB_GETCOUNT, 0, 0)) {
        for (int a = 0x200; a <= 0x20F; a++) { char b[8]; snprintf(b, sizeof b, "%X", a); cb_add(p, IDC_J_ADDR, b, a); }
        for (int i = 0; i < 4; i++) cb_add(p, IDC_J_TYPE, types[i], i);
        cb_add(p, IDC_J_MODE, "the pad: 6 axes, D-pad hat, 12 buttons", 0);
        cb_add(p, IDC_J_MODE, "the game port as DOS sees it", 1);
        SendDlgItemMessageA(p, IDC_J_MODE, CB_SETDROPPEDWIDTH, 170, 0);
    }
    JoyOutSettings js;
    joyout_load(g_inipath, js);
    check(p, IDC_J_PORTON, ini_int("Joystick", 1) != 0);
    cb_select(p, IDC_J_ADDR, (int)ini_port());
    cb_select(p, IDC_J_TYPE, js.type);
    check(p, IDC_J_BIOS, ini_int("JoyBios", 1) != 0);
    check(p, IDC_J_WINPORT, ini_get("Joystick", "WindowsPort", 0) != 0);
    check(p, IDC_J_NUKED, js.nuked != 0);
    cb_select(p, IDC_J_MODE, js.mode);
    set_int(p, IDC_J_DZ, js.deadzone);
    joy_page_status(p);
}

static bool joy_page_save(HWND p, std::string& note)
{
    char b[16];
    WritePrivateProfileStringA("SC55W", "Joystick", checked(p, IDC_J_PORTON) ? "1" : "0", g_inipath.c_str());
    WritePrivateProfileStringA("SC55W", "JoyBios", checked(p, IDC_J_BIOS) ? "1" : "0", g_inipath.c_str());
    unsigned port = (unsigned)cb_get(p, IDC_J_ADDR, 0x201);
    snprintf(b, sizeof b, "%X", port);
    WritePrivateProfileStringA("Joystick", "Port", b, g_inipath.c_str());
    ini_put("Joystick", "WindowsPort", checked(p, IDC_J_WINPORT));
    JoyOutSettings js;
    js.type = cb_get(p, IDC_J_TYPE, 0);
    js.nuked = checked(p, IDC_J_NUKED);
    js.mode = cb_get(p, IDC_J_MODE, 0);
    js.deadzone = get_int(p, IDC_J_DZ, 0, 50, 10);
    joyout_save(g_inipath, js);
    InterlockedExchange(&g_joyout_dirty, 1);
    if (gameport_set_port(port)) {               // the Device Manager entry follows the address
        note += "Device Manager shows the NUKED game port at its new address after Windows is restarted "
                "(DOS boxes use it now).\r\n";
        return true;
    }
    return false;
}

// live test: what Windows gives (the pad), what DOS boxes get (the game port), NUKED joystick
static void draw_stick(HDC dc, int x, int y, int w, int vx, int vy, const char* label)
{
    Rectangle(dc, x, y, x + w, y + w);
    MoveToEx(dc, x + w / 2, y, NULL); LineTo(dc, x + w / 2, y + w);
    MoveToEx(dc, x, y + w / 2, NULL); LineTo(dc, x + w, y + w / 2);
    int px = x + 2 + (int)((long)(vx + 32768) * (w - 5) / 65535), py = y + 2 + (int)((long)(vy + 32768) * (w - 5) / 65535);
    HBRUSH br = CreateSolidBrush(RGB(200, 0, 0));
    HGDIOBJ ob = SelectObject(dc, br);
    Ellipse(dc, px - 3, py - 3, px + 4, py + 4);
    SelectObject(dc, ob);
    DeleteObject(br);
    TextOutA(dc, x, y + w + 1, label, (int)strlen(label));
}

static void joy_view_draw(const DRAWITEMSTRUCT* di)
{
    HDC dc = di->hDC;
    RECT r = di->rcItem;
    FillRect(dc, &r, (HBRUSH)(COLOR_BTNFACE + 1));
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ of = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    PadInput in;
    std::string name;
    EnterCriticalSection(&g_cs);
    in = g_padin[0];
    name = g_padname[0];
    LeaveCriticalSection(&g_cs);
    char b[200];
    snprintf(b, sizeof b, "Pad: %s", in.present ? name.c_str() : "none (Control Panel > Game Controllers)");
    TextOutA(dc, r.left, r.top, b, (int)strlen(b));
    int y = r.top + 16, w = 48;
    draw_stick(dc, r.left + 2, y, w, in.axis[PA_LX], in.axis[PA_LY], "Left");
    draw_stick(dc, r.left + 62, y, w, in.axis[PA_RX], in.axis[PA_RY], "Right");
    // buttons
    static const char* bn[PB_COUNT] = {"A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS", "Back", "Start",
                                       "Up", "Down", "Left", "Right"};
    int bx = r.left + 122, by = y;
    for (int i = 0; i < PB_COUNT; i++) {
        RECT br = {bx + (i % 2) * 28, by + (i / 2) * 11, bx + (i % 2) * 28 + 26, by + (i / 2) * 11 + 10};
        if (in.button[i]) FillRect(dc, &br, (HBRUSH)GetStockObject(GRAY_BRUSH));
        else FrameRect(dc, &br, (HBRUSH)GetStockObject(GRAY_BRUSH));
        TextOutA(dc, br.left + 2, br.top - 1, bn[i], (int)strlen(bn[i]));
    }
    // DOS game port
    y = r.top + 96;
    if (g_sh) {
        snprintf(b, sizeof b, "DOS game port %X: %u %u %u %u  btn %c%c%c%c",
                 ini_port(), g_sh->joy_axis[0] >> 8, g_sh->joy_axis[1] >> 8, g_sh->joy_axis[2] >> 8,
                 g_sh->joy_axis[3] >> 8, g_sh->joy_buttons & 1 ? '1' : '-', g_sh->joy_buttons & 2 ? '2' : '-',
                 g_sh->joy_buttons & 4 ? '3' : '-', g_sh->joy_buttons & 8 ? '4' : '-');
        TextOutA(dc, r.left, y, b, (int)strlen(b));
        snprintf(b, sizeof b, "  read %lu times by DOS programs", (unsigned long)g_sh->joy_reads);
        TextOutA(dc, r.left, y + 12, b, (int)strlen(b));
        const SC55W_WJOY& w0 = g_sh->wjoy[0];
        if (w0.present) {
            snprintf(b, sizeof b, "NUKED joystick: X %u Y %u Z %u R %u U %u",
                     w0.axis[0] >> 8, w0.axis[1] >> 8, w0.axis[2] >> 8, w0.axis[3] >> 8, w0.axis[4] >> 8);
            TextOutA(dc, r.left, y + 28, b, (int)strlen(b));
            std::string bt;
            for (int k = 0; k < 12; k++) if (w0.buttons >> k & 1) { char n[8]; snprintf(n, sizeof n, "%d ", k + 1); bt += n; }
            snprintf(b, sizeof b, "  hat %s, buttons %s", w0.pov == 0xFFFF ? "centre" : w0.pov == 0 ? "up" :
                     w0.pov == 9000 ? "right" : w0.pov == 18000 ? "down" : w0.pov == 27000 ? "left" : "diagonal",
                     bt.empty() ? "none" : bt.c_str());
            TextOutA(dc, r.left, y + 40, b, (int)strlen(b));
        } else TextOutA(dc, r.left, y + 28, "NUKED joystick: off", 19);
    } else TextOutA(dc, r.left, y, "SC55W.VXD is not attached", 25);
    SelectObject(dc, of);
}

static INT_PTR CALLBACK joy_page_proc(HWND p, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_INITDIALOG:
        joy_page_load(p);
        SetTimer(p, 1, 100, NULL);
        return TRUE;
    case WM_TIMER:
        if (IsWindowVisible(p)) {
            InvalidateRect(GetDlgItem(p, IDC_J_VIEW), NULL, FALSE);
            static int n;
            if (++n % 10 == 0) joy_page_status(p);
        }
        return TRUE;
    case WM_DRAWITEM:
        if (w == IDC_J_VIEW) { joy_view_draw((const DRAWITEMSTRUCT*)l); return TRUE; }
        return FALSE;
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDC_J_SETUP: {
            std::string log;
            unsigned port = (unsigned)cb_get(p, IDC_J_ADDR, 0x201);
            bool restart = gameport_install(path_join(g_exedir, "JOY"), port, log);
            logf("game port setup: %s", log.c_str());
            joy_page_status(p);
            MessageBoxA(p, (log + (restart ? "\r\nRestart Windows so that it loads the NUKED game port."
                                           : "")).c_str(), APP_TITLE, MB_ICONINFORMATION);
            return TRUE;
        }
        case IDC_J_REMOVE: {
            if (MessageBoxA(p, "Remove the NUKED SB-EMU Game Port and the NUKED SB-EMU Joystick from Windows?",
                            APP_TITLE, MB_YESNO | MB_ICONQUESTION) != IDYES) return TRUE;
            std::string log;
            gameport_remove(log);
            logf("game port removed: %s", log.c_str());
            joy_page_status(p);
            MessageBoxA(p, (log + "\r\nRestart Windows to finish.").c_str(), APP_TITLE, MB_ICONINFORMATION);
            return TRUE;
        }
        case IDC_J_CPL:
            ShellExecuteA(p, "open", "control.exe", "joy.cpl", NULL, SW_SHOWNORMAL);
            return TRUE;
        }
        if (HIWORD(w) == CBN_SELCHANGE || HIWORD(w) == BN_CLICKED || HIWORD(w) == EN_CHANGE)
            SendMessageA(GetParent(p), WM_APP + 50, 0, 0);       // the main dialog: something changed
        return FALSE;
    }
    return FALSE;
}

// --- Pad keys page
static const char* const AXES[] = {"none", "X", "Y", "Z", "R", "U", "V"};
static void pad_page_load(HWND p)
{
    if (!SendDlgItemMessageA(p, IDC_P_TRIG, CB_GETCOUNT, 0, 0))
        for (int i = 0; i < 7; i++) {
            cb_add(p, IDC_P_TRIG, AXES[i], i);
            if (i) { cb_add(p, IDC_P_RX, AXES[i], i); cb_add(p, IDC_P_RY, AXES[i], i); }
        }
    check(p, IDC_P_KEYS, ini_int("PadKeys", 1) != 0);
    for (int i = 0; i < PB_COUNT; i++) set_int(p, IDC_P_B0 + i, g_padmap.btn[i]);
    auto axis_idx = [](char c) { for (int i = 1; i < 7; i++) if (AXES[i][0] == c) return i; return 0; };
    cb_select(p, IDC_P_TRIG, axis_idx(g_padmap.trig));
    cb_select(p, IDC_P_RX, axis_idx(g_padmap.rx) ? axis_idx(g_padmap.rx) : 5);
    cb_select(p, IDC_P_RY, axis_idx(g_padmap.ry) ? axis_idx(g_padmap.ry) : 4);
}

static void pad_page_save(HWND p)
{
    WritePrivateProfileStringA("SC55W", "PadKeys", checked(p, IDC_P_KEYS) ? "1" : "0", g_inipath.c_str());
    for (int i = 0; i < PB_COUNT; i++) ini_put("Pad", pad_button_name(i), get_int(p, IDC_P_B0 + i, 0, 32, 0));
    WritePrivateProfileStringA("Pad", "Triggers", AXES[cb_get(p, IDC_P_TRIG, 3)], g_inipath.c_str());
    WritePrivateProfileStringA("Pad", "RStickX", AXES[cb_get(p, IDC_P_RX, 5)], g_inipath.c_str());
    WritePrivateProfileStringA("Pad", "RStickY", AXES[cb_get(p, IDC_P_RY, 4)], g_inipath.c_str());
    pad_map_load();
    // keys per game only work with the INT 21h watch, which needs a new attach (push_options)
    g_padkeys = checked(p, IDC_P_KEYS) && g_vxd != INVALID_HANDLE_VALUE;
    if (g_padkeys) pad_files_load();
}

static INT_PTR CALLBACK pad_page_proc(HWND p, UINT m, WPARAM w, LPARAM l)
{
    (void)l;
    switch (m) {
    case WM_INITDIALOG:
        pad_page_load(p);
        SetTimer(p, 1, 100, NULL);
        return TRUE;
    case WM_TIMER:
        if (IsWindowVisible(p)) {
            static DWORD prev;
            DWORD raw;
            std::string t, pr, la;
            EnterCriticalSection(&g_cs);
            raw = g_rawbtn; t = g_pad_title; pr = g_pad_prog; la = g_pad_last;
            LeaveCriticalSection(&g_cs);
            // learn: a box has the focus and a new Windows button goes down
            HWND f = GetFocus();
            int id = f ? GetDlgCtrlID(f) : 0;
            DWORD nb = raw & ~prev;
            prev = raw;
            if (GetParent(f) == p && id >= IDC_P_B0 && id <= IDC_P_B15 && nb) {
                for (int k = 0; k < 32; k++) if (nb >> k & 1) { set_int(p, id, k + 1); break; }
                if (id < IDC_P_B15) SetFocus(GetDlgItem(p, id + 1));
            }
            std::string s = "Layout now: " + t + (pr.empty() ? std::string("") : "  (for " + pr + ")") +
                            "\r\nLast pad button: " + la;
            char cur[300];
            GetDlgItemTextA(p, IDC_P_STAT, cur, sizeof cur);
            if (s != cur) SetDlgItemTextA(p, IDC_P_STAT, s.c_str());
            char b[64];
            snprintf(b, sizeof b, "Windows buttons down now: ");
            std::string d = b;
            for (int k = 0; k < 32; k++) if (raw >> k & 1) { snprintf(b, sizeof b, "%d ", k + 1); d += b; }
            GetDlgItemTextA(p, IDC_P_LAST, cur, sizeof cur);
            if (d != cur) SetDlgItemTextA(p, IDC_P_LAST, d.c_str());
        }
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDC_P_EDIT: {
            std::string f = path_join(g_base, "PADUSER.TXT");
            if (!is_file(f)) save_text(f, "; PADUSER.TXT - your own pad layouts (they win over PADMAP.TXT).\r\n"
                                          "; See README98.TXT, \"Game pad buttons as keys\".\r\n");
            ShellExecuteA(p, "open", "notepad.exe", f.c_str(), NULL, SW_SHOWNORMAL);
            return TRUE;
        }
        case IDC_P_FOLDER: {
            std::string f = path_join(g_base, "PADMAP");
            CreateDirectoryA(f.c_str(), NULL);
            ShellExecuteA(p, "open", f.c_str(), NULL, NULL, SW_SHOWNORMAL);
            return TRUE;
        }
        case IDC_P_DEF: {
            static const int def[PB_COUNT] = {1, 2, 3, 4, 5, 6, 0, 0, 9, 10, 7, 8, 0, 0, 0, 0};
            for (int i = 0; i < PB_COUNT; i++) set_int(p, IDC_P_B0 + i, def[i]);
            cb_select(p, IDC_P_TRIG, 3); cb_select(p, IDC_P_RX, 5); cb_select(p, IDC_P_RY, 4);
            return TRUE;
        }
        }
        if (HIWORD(w) == CBN_SELCHANGE || HIWORD(w) == BN_CLICKED || HIWORD(w) == EN_CHANGE)
            SendMessageA(GetParent(p), WM_APP + 50, 0, 0);
        return FALSE;
    }
    return FALSE;
}

static void pages_create(HWND d)
{
    HWND tab = GetDlgItem(d, IDC_TABS);
    const char* names[] = {"Sound and MIDI", "Joystick", "Pad keys"};
    for (int i = 0; i < 3; i++) {
        TCITEMA it;
        memset(&it, 0, sizeof it);
        it.mask = TCIF_TEXT;
        it.pszText = (char*)names[i];
        SendMessageA(tab, TCM_INSERTITEMA, i, (LPARAM)&it);
    }
    RECT r;
    GetWindowRect(tab, &r);
    MapWindowPoints(NULL, d, (POINT*)&r, 2);
    SendMessageA(tab, TCM_ADJUSTRECT, FALSE, (LPARAM)&r);
    g_page[1] = CreateDialogParamA(g_inst, MAKEINTRESOURCEA(IDD_PAGE_JOY), d, joy_page_proc, 0);
    g_page[2] = CreateDialogParamA(g_inst, MAKEINTRESOURCEA(IDD_PAGE_PAD), d, pad_page_proc, 0);
    for (int i = 1; i < 3; i++)
        SetWindowPos(g_page[i], HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_HIDEWINDOW);
    SendMessageA(tab, TCM_SETCURSEL, g_tab, 0);
    tab_show(d);
}

static INT_PTR CALLBACK settings_proc(HWND d, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_INITDIALOG:
        g_dlg_opt = g_opt;
        g_page[1] = g_page[2] = NULL;
        pages_create(d);
        settings_fill_lists(d);
        settings_show(d, g_dlg_opt);
        check(d, IDC_AUTOSTART, autostart_get());
        settings_update(d);
        SendMessageA(d, WM_SETICON, ICON_BIG, (LPARAM)LoadIconA(g_inst, MAKEINTRESOURCEA(IDI_APP)));
        return TRUE;
    case WM_COMMAND: {
        int id = LOWORD(w), code = HIWORD(w);
        switch (id) {
        case IDOK:
            settings_apply(d, true);
            DestroyWindow(d);
            return TRUE;
        case IDCANCEL:
            if (settings_read(d).format() != g_opt.format() || g_dlg_opt.format() != g_opt.format())
                push_options(g_opt);              // undo an Apply that was not saved
            DestroyWindow(d);
            return TRUE;
        case IDC_APPLY:
            settings_apply(d, false);
            return TRUE;
        case IDC_DEFAULTS: {
            Options def;
            def.passthru = g_dlg_opt.passthru;
            def.scrom = g_dlg_opt.scrom;
            settings_show(d, def);
            settings_update(d);
            return TRUE;
        }
        case IDC_SFBROWSE: {
            char file[MAX_PATH] = "";
            std::string init = path_join(g_base, "SF2");
            OPENFILENAMEA ofn;
            memset(&ofn, 0, sizeof ofn);
            ofn.lStructSize = OPENFILENAME_SIZE_VERSION_400A;   // Win98 comdlg32 wants the old size
            ofn.hwndOwner = d;
            ofn.lpstrFilter = "SoundFont (*.SF2)\0*.SF2\0All files\0*.*\0";
            ofn.lpstrFile = file;
            ofn.nMaxFile = sizeof file;
            ofn.lpstrInitialDir = init.c_str();
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
            if (GetOpenFileNameA(&ofn)) {
                std::string p = short_path(file);       // DOS SBEMU reads the same path
                SetDlgItemTextA(d, IDC_SF, p.c_str());
                settings_update(d);
            }
            return TRUE;
        }
        }
        if (!g_dlg_filling && (code == CBN_SELCHANGE || code == EN_CHANGE || code == BN_CLICKED))
            settings_update(d);
        return FALSE;
    }
    case WM_NOTIFY:
        if (((NMHDR*)l)->idFrom == IDC_TABS && ((NMHDR*)l)->code == (UINT)TCN_SELCHANGE) { tab_show(d); return TRUE; }
        return FALSE;
    case WM_CLOSE:
        SendMessageA(d, WM_COMMAND, IDCANCEL, 0);
        return TRUE;
    case WM_DESTROY:
        g_settings_dlg = NULL;
        g_page[1] = g_page[2] = NULL;
        return TRUE;
    }
    return FALSE;
}

static void show_settings()
{
    if (g_settings_dlg) { SetForegroundWindow(g_settings_dlg); return; }
    g_settings_dlg = CreateDialogParamA(g_inst, MAKEINTRESOURCEA(IDD_SETTINGS), NULL, settings_proc, 0);
    if (g_settings_dlg) {
        ShowWindow(g_settings_dlg, SW_SHOW);
        SetForegroundWindow(g_settings_dlg);
    }
}

// ---------------------------------------------------------------------------
// status dialog
static INT_PTR CALLBACK status_proc(HWND d, UINT m, WPARAM w, LPARAM l)
{
    (void)l;
    switch (m) {
    case WM_INITDIALOG:
        SetDlgItemTextA(d, IDC_STATUSTEXT, status_text().c_str());
        SetTimer(d, 1, 500, NULL);
        SendMessageA(d, WM_SETICON, ICON_BIG, (LPARAM)LoadIconA(g_inst, MAKEINTRESOURCEA(IDI_APP)));
        return TRUE;
    case WM_TIMER:
        SetDlgItemTextA(d, IDC_STATUSTEXT, status_text().c_str());
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDC_PANIC: InterlockedExchange(&g_panic, 1); return TRUE;
        case IDM_SETTINGS: show_settings(); return TRUE;
        case IDCANCEL: DestroyWindow(d); return TRUE;
        }
        return FALSE;
    case WM_CLOSE:
        DestroyWindow(d);
        return TRUE;
    case WM_DESTROY:
        KillTimer(d, 1);
        g_status_dlg = NULL;
        return TRUE;
    }
    return FALSE;
}

static void show_status()
{
    if (g_status_dlg) { SetForegroundWindow(g_status_dlg); return; }
    g_status_dlg = CreateDialogParamA(g_inst, MAKEINTRESOURCEA(IDD_STATUS), NULL, status_proc, 0);
    if (g_status_dlg) {
        ShowWindow(g_status_dlg, SW_SHOW);
        SetForegroundWindow(g_status_dlg);
    }
}

// ---------------------------------------------------------------------------
// shutdown: stop the audio thread, release the VxD (the DOS ports go back)
static bool g_stopped;
static void shutdown_all()
{
    if (g_stopped) return;
    g_stopped = true;
    InterlockedExchange(&g_quit, 1);
    logf("exit requested");
    set_step("ok");             // a normal exit (or Windows shutting down) is not a freeze
    if (g_thread) {
        SetEvent(g_wev);
        if (WaitForSingleObject(g_thread, 5000) == WAIT_TIMEOUT) { logf("audio thread did not stop"); TerminateThread(g_thread, 0); }
        CloseHandle(g_thread);
        g_thread = NULL;
    }
    if (g_vxd != INVALID_HANDLE_VALUE) {
        DWORD got;
        DeviceIoControl(g_vxd, IOCTL_SC55W_DETACH, NULL, 0, NULL, 0, &got, NULL);
        CloseHandle(g_vxd);
        g_vxd = INVALID_HANDLE_VALUE;
    }
    Shell_NotifyIconA(NIM_DELETE, &g_nid);
    logf("stopped");
}

static LRESULT CALLBACK main_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == g_taskbar_created && m) { tray_add(); tray_tip(); return 0; }
    switch (m) {
    case WM_TRAY:
        if (l == WM_LBUTTONDBLCLK) show_status();
        else if (l == WM_RBUTTONUP || l == WM_LBUTTONUP) {
            if (l == WM_LBUTTONUP) return 0;
            HMENU mn = CreatePopupMenu();
            AppendMenuA(mn, MF_STRING, IDM_STATUS, "&Status...");
            AppendMenuA(mn, MF_STRING, IDM_SETTINGS, "S&ettings...");
            AppendMenuA(mn, MF_STRING, IDM_PANIC, "&All notes off");
            AppendMenuA(mn, MF_SEPARATOR, 0, NULL);
            AppendMenuA(mn, MF_STRING, IDM_EXIT, "E&xit");
            SetMenuDefaultItem(mn, IDM_STATUS, FALSE);
            POINT p;
            GetCursorPos(&p);
            SetForegroundWindow(h);
            TrackPopupMenu(mn, TPM_RIGHTBUTTON, p.x, p.y, 0, h, NULL);
            PostMessageA(h, WM_NULL, 0, 0);
            DestroyMenu(mn);
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDM_STATUS: show_status(); break;
        case IDM_SETTINGS: show_settings(); break;
        case IDM_PANIC: InterlockedExchange(&g_panic, 1); break;
        case IDM_EXIT: DestroyWindow(h); break;
        }
        return 0;
    case WM_REMOTE:
        if (w == 1) show_settings();
        else if (w == 2) show_status();
        else if (w == 3) DestroyWindow(h);
        return 0;
    case WM_STATECHG:
        tray_tip();
        return 0;
    case WM_ENDSESSION:
        if (w) shutdown_all();
        return 0;
    case WM_DESTROY:
        shutdown_all();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR cmd, int)
{
    g_inst = inst;
    InitCommonControls();                      // the settings window's tabs
    std::string c = cmd ? cmd : "";
    for (auto& ch : c) ch = (char)toupper((unsigned char)ch);
    int remote = c.find("/SETTINGS") != std::string::npos ? 1 : c.find("/STATUS") != std::string::npos ? 2
               : c.find("/EXIT") != std::string::npos ? 3 : 0;

    if (c.find("/INSTALL") != std::string::npos) return run_install(inst);
    g_boot = c.find("/BOOT") != std::string::npos;
    if (c.find("/UNINSTALL") != std::string::npos) return run_uninstall(inst);

    // one copy only; a second start forwards its request to the first
    HANDLE mx = CreateMutexA(NULL, FALSE, "SC55W_Helper_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowA(WND_CLASS, NULL);
        if (other) PostMessageA(other, WM_REMOTE, remote ? remote : 2, 0);
        if (remote == 3)       // /EXIT: return once the helper has gone (for SETUP98 / UNINST98)
            for (int i = 0; i < 100 && FindWindowA(WND_CLASS, NULL); i++) Sleep(100);
        return 0;
    }
    if (remote == 3) return 0;

    InitializeCriticalSection(&g_cs);
    find_paths();
    load_options();

    WNDCLASSA wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = main_proc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconA(inst, MAKEINTRESOURCEA(IDI_APP));
    wc.lpszClassName = WND_CLASS;
    RegisterClassA(&wc);
    g_wnd = CreateWindowA(WND_CLASS, APP_TITLE, WS_OVERLAPPED, 0, 0, 0, 0, NULL, NULL, inst, NULL);
    g_taskbar_created = RegisterWindowMessageA("TaskbarCreated");
    tray_add();

    logf("%s build %d, pack folder %s, options: %s", APP_TITLE, BUILD_ID, g_base.c_str(), g_opt.format().c_str());
    {   // the system, for bug reports
        OSVERSIONINFOA v; v.dwOSVersionInfoSize = sizeof v;
        if (GetVersionExA(&v)) logf("Windows %lu.%lu build %lu %s", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber & 0xFFFF, v.szCSDVersion);
        const char* bl = getenv("BLASTER");
        logf("BLASTER = %s", bl ? bl : "(not set)");
        MEMORYSTATUS ms; ms.dwLength = sizeof ms; GlobalMemoryStatus(&ms);
        logf("memory: %lu MB total, %lu MB free", (unsigned long)(ms.dwTotalPhys >> 20), (unsigned long)(ms.dwAvailPhys >> 20));
        UINT n = waveOutGetNumDevs();
        for (UINT i = 0; i < n; i++) { WAVEOUTCAPSA c; if (waveOutGetDevCapsA(i, &c, sizeof c) == 0) logf("wave out %u: %s", i, c.szPname); }
        n = midiOutGetNumDevs();
        for (UINT i = 0; i < n; i++) { MIDIOUTCAPSA c; if (midiOutGetDevCapsA(i, &c, sizeof c) == 0) logf("MIDI out %u: %s", i, c.szPname); }
    }
    {   // did the last start freeze Windows?
        std::string last = get_step();
        if (!last.empty() && last != "ok") {
            logf("the last start did not finish (stopped at: %s)", last.c_str());
            if (g_boot) {
                set_step("ok");     // next boot tries again
                logf("skipped this start so Windows can start");
                MessageBoxA(NULL, ("NUKED SB-EMU did not finish starting last time (it stopped at: " + last +
                    "), so it was not started with Windows this time.\r\n\r\nStart it from C:\\SC55\\WIN98\\SC55W.EXE "
                    "when you need it, and if Windows freezes again, send C:\\SC55\\WIN98\\SC55W.LOG.").c_str(),
                    APP_TITLE, MB_ICONEXCLAMATION | MB_SETFOREGROUND);
                NOTIFYICONDATAA nd; memset(&nd, 0, sizeof nd); nd.cbSize = sizeof nd; nd.hWnd = g_wnd; nd.uID = 1;
                Shell_NotifyIconA(NIM_DELETE, &nd);
                return 0;
            }
        }
    }
    set_step("vxd");
    if (vxd_open()) logf("SC55W.VXD loaded, build %lu", g_vxd_build);
    else logf("VxD: %s", g_vxd_error.c_str());
    g_wev = CreateEventA(NULL, FALSE, FALSE, NULL);
    push_options(g_opt);
    DWORD tid;
    g_thread = CreateThread(NULL, 0, audio_main, NULL, 0, &tid);
    g_padkeys = ini_int("PadKeys", 1) != 0 && g_vxd != INVALID_HANDLE_VALUE;
    pad_map_load();
    if (g_padkeys) pad_files_load();
    if (ini_int("Joystick", 1) || ini_int("JoyBios", 1) || g_padkeys) {
        HANDLE jt = CreateThread(NULL, 0, joy_main, NULL, 0, &tid);
        if (jt) { SetThreadPriority(jt, THREAD_PRIORITY_ABOVE_NORMAL); CloseHandle(jt); }
    }

    // report DOS-side problems once at start (port taken by another driver, ...)
    if (g_vxd == INVALID_HANDLE_VALUE) {
        MessageBoxA(NULL, ("DOS programs will get no Sound Blaster or MPU-401:\r\n" + g_vxd_error +
                           "\r\n\r\nThe Windows MIDI device needs it too.").c_str(), APP_TITLE, MB_ICONEXCLAMATION);
    } else {
        for (int i = 0; i < 100; i++) {          // wait up to 10 s for the attach result
            EnterCriticalSection(&g_cs);
            DWORD st = g_pub.attach;
            LeaveCriticalSection(&g_cs);
            if (st) {
                std::string pr = attach_problems(g_opt, st);
                if (!pr.empty())
                    MessageBoxA(NULL, ("Some of the DOS sound hardware could not be set up:\r\n\r\n" + pr +
                                       "\r\nChange the settings, or turn off the other driver's Sound Blaster "
                                       "emulation.").c_str(), APP_TITLE, MB_ICONEXCLAMATION);
                break;
            }
            Sleep(100);
        }
    }
    if (remote == 1) show_settings();
    else if (remote == 2) show_status();

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        if (g_settings_dlg && msg.message == WM_KEYDOWN && (msg.wParam == VK_TAB || msg.wParam == VK_NEXT ||
            msg.wParam == VK_PRIOR) && (GetKeyState(VK_CONTROL) & 0x8000) &&
            (msg.hwnd == g_settings_dlg || IsChild(g_settings_dlg, msg.hwnd))) {
            // Ctrl+Tab / Ctrl+PgDn: next tab, Ctrl+Shift+Tab / Ctrl+PgUp: previous (as property sheets)
            HWND tab = GetDlgItem(g_settings_dlg, IDC_TABS);
            int n = (int)SendMessageA(tab, TCM_GETITEMCOUNT, 0, 0), i = (int)SendMessageA(tab, TCM_GETCURSEL, 0, 0);
            bool back = msg.wParam == VK_PRIOR || (msg.wParam == VK_TAB && (GetKeyState(VK_SHIFT) & 0x8000));
            SendMessageA(tab, TCM_SETCURSEL, (i + (back ? n - 1 : 1)) % n, 0);
            tab_show(g_settings_dlg);
            continue;
        }
        if (g_settings_dlg && IsDialogMessageA(g_settings_dlg, &msg)) continue;
        if (g_status_dlg && IsDialogMessageA(g_status_dlg, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    shutdown_all();
    CloseHandle(mx);
    logf("exit");
    return 0;
}
