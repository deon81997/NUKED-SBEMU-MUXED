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
    timeBeginPeriod(1);
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

    while (!g_quit) {
        if (InterlockedExchange(&g_have_pending, 0)) {
            EnterCriticalSection(&g_cs);
            Options want = g_pending;
            LeaveCriticalSection(&g_cs);
            SC55W_CONFIG a = config_for(want), b = config_for(cur);
            if (!have || memcmp(&a, &b, sizeof a)) {
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

        WaitForSingleObject(g_wev, 50);
        int queued = 0;
        for (auto& h : g_hdr) if (!(h.dwFlags & WHDR_DONE)) queued++;
        if (queued == 0) underruns++;
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

        // /VSCAUTO: shed the SC-55's reverb/chorus when the CPU cannot keep up
        DWORD now = GetTickCount();
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
    timeEndPeriod(1);
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
    return m;
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
    s += "MIDI synth: " + (p.synth_error.empty() ? p.synth : "none - " + p.synth_error) + "\r\n";
    s += midi_driver_installed() ? "Windows MIDI device: \"NUKED SB-EMU\" (installed)\r\n"
                                 : "Windows MIDI device: not installed (run SETUP98)\r\n";
    if (!p.wave_error.empty()) s += p.wave_error + "\r\n";
    else {
        snprintf(b, sizeof b, "Output: %d Hz, %d ms buffer, CPU %d%%%s%s\r\n", p.rate, p.buf_ms,
                 (int)(p.load * 100 + 0.5), p.shed ? " (reverb/chorus paused)" : "",
                 p.st.midi_idle ? ", synth idle" : "");
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

static void settings_apply(HWND d, bool save)
{
    Options o = settings_read(d);
    g_dlg_opt = o;
    g_opt = o;
    push_options(o);
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

static INT_PTR CALLBACK settings_proc(HWND d, UINT m, WPARAM w, LPARAM l)
{
    (void)l;
    switch (m) {
    case WM_INITDIALOG:
        g_dlg_opt = g_opt;
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
    case WM_CLOSE:
        SendMessageA(d, WM_COMMAND, IDCANCEL, 0);
        return TRUE;
    case WM_DESTROY:
        g_settings_dlg = NULL;
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
