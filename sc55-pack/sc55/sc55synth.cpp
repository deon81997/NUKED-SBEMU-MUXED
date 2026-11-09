// sc55synth.cpp - binds the (unmodified) Nuked-SC55 0.7.0 backend to the DOS
// port's engine (sc55engine.hpp), and finds/validates the user's ROM dumps.
//
// ROMs are identified by SHA-256 of their contents, never by file name, exactly
// like the Win98 VSC-55 host. No ROMs are shipped with this project.
//
// GPL-2.0-or-later.
#include "sc55synth.h"
#include "sc55engine.hpp"

#include "diagnostics.h"
#include "emu.h"
#include "lcd.h"
namespace {
struct NullLCD final : LCD_Backend
{
    bool Start(const lcd_t&) override { return true; }
    void Stop() override {}
    void Render() override {}
};
NullLCD g_null_lcd;
}
#include "rom_io.h"
#include "sha256.h"
#include "standard_romsets.h"

#include <dirent.h>
#include <sys/stat.h>
#ifdef __DJGPP__
#include <dos.h>
#endif

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <exception>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace
{

struct RealCore
{
    std::unique_ptr<Emulator> emu;
    void (*sink)(void*, int32_t, int32_t) = nullptr;
    void* ud = nullptr;

    static void trampoline(void* self, const AudioFrame<int32_t>& f)
    {
        auto* c = static_cast<RealCore*>(self);
        c->sink(c->ud, f.left, f.right);
    }
    void set_sample_sink(void (*fn)(void*, int32_t, int32_t), void* u)
    {
        sink = fn;
        ud = u;
        if (emu) emu->SetSampleCallback(&RealCore::trampoline, this);
    }
    void step() { emu->Step(); }
    // Never fill the core's UART ring completely: equal read/write pointers mean empty.
    unsigned uart_free()
    {
        const mcu_t& m = emu->GetMCU();
        const uint32_t pending = (m.uart_write_ptr + uart_buffer_size - m.uart_read_ptr) % uart_buffer_size;
        return uart_buffer_size - 1 - pending;
    }
    void uart_put(uint8_t b) { emu->PostMIDI(b); }
};

using Eng = sc55::Engine<RealCore>;

RealCore g_core;
alignas(16) unsigned char g_engine_storage[sizeof(Eng)];
Eng* g_engine = nullptr;
RomsetInfo g_info;
char g_err[320] = "";
char g_model[64] = "";

void fail(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof(g_err), fmt, ap);
    va_end(ap);
}

bool ieq(const char* a, const char* b)
{
    for (; *a && *b; ++a, ++b)
    {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a');
        if (x != y) return false;
    }
    return *a == *b;
}

bool sc55_family(Romset r)
{
    return r == Romset::MK1 || r == Romset::MK2 || r == Romset::ST || r == Romset::SC155 ||
           r == Romset::SC155MK2;
}

struct Found
{
    std::string path;
    SHA256_Digest digest;
};

std::string join_path(const char* dir, const char* name)
{
    std::string p = dir;
    if (!p.empty() && p.back() != '\\' && p.back() != '/' && p.back() != ':') p += '/'; // DJGPP accepts '/' as a separator
    return p + name;
}

bool read_file(const std::string& path, std::vector<uint8_t>& out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > 16 * 1024 * 1024)
    {
        fclose(f);
        return false;
    }
    out.resize((size_t)n);
    bool ok = fread(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}

// ROMHASH.CAC in the ROM folder remembers each file's size, date and SHA-256, so the ROMs (about 10 MB)
// are only read and hashed again when a file changed. Lines: <name> <size> <mtime> <64 hex digits>.
// If the folder is read-only the cache is simply not written and every start hashes, as before.
static const char* kHashCache = "ROMHASH.CAC";
struct CacheEntry { std::string name; long size; long mtime; SHA256_Digest digest; };

static bool hex_digest(const char* hex, SHA256_Digest& d)
{
    for (size_t i = 0; i < d.size(); i++)
    {
        unsigned v;
        if (sscanf(hex + 2 * i, "%2x", &v) != 1) return false;
        d[i] = (uint8_t)v;
    }
    return true;
}

static void load_cache(const char* dir, std::vector<CacheEntry>& out)
{
    FILE* f = fopen(join_path(dir, kHashCache).c_str(), "r");
    char line[256], name[64], hex[80];
    if (!f) return;
    while (fgets(line, sizeof line, f))
    {
        CacheEntry c; long sz, mt;
        if (sscanf(line, "%63s %ld %ld %79s", name, &sz, &mt, hex) != 4 || strlen(hex) != 64) continue;
        if (!hex_digest(hex, c.digest)) continue;
        c.name = name; c.size = sz; c.mtime = mt;
        out.push_back(c);
    }
    fclose(f);
}

static void save_cache(const char* dir, const std::vector<CacheEntry>& entries)
{
    std::string path = join_path(dir, kHashCache), tmp = join_path(dir, "ROMHASH.$$$");
    FILE* f = fopen(tmp.c_str(), "w");
    if (!f) return;                                   /* read-only folder: no cache */
    bool ok = true;
    for (const CacheEntry& c : entries)
    {
        if (fprintf(f, "%s %ld %ld ", c.name.c_str(), c.size, c.mtime) < 0) ok = false;
        for (uint8_t b : c.digest) fprintf(f, "%02x", b);
        fprintf(f, "\n");
    }
    if (fflush(f) != 0) ok = false;
    if (fclose(f) != 0) ok = false;
    if (!ok) { remove(tmp.c_str()); return; }
    remove(path.c_str());
    rename(tmp.c_str(), path.c_str());
}

// The file's date for the cache. Until beta.72 this was stat()'s st_mtime, but DJGPP's stat() gave the same
// file different times depending on how SBEMU was started (seen in 86Box: Nov 7 23:30:08 from one batch file,
// Dec 14 03:06:34 from another), so the cache missed and every ROM was hashed again: about 6 s of start-up on
// a 350 MHz PC. On DOS the raw FAT date and time from the open handle (INT 21h AX=5700h) are used instead.
static long file_stamp(const std::string& path, const struct stat& st)
{
#ifdef __DJGPP__
    int h;
    if (_dos_open(path.c_str(), 0 /* read */, &h) == 0)
    {
        unsigned int date = 0, time = 0;
        const unsigned rc = _dos_getftime(h, &date, &time);
        _dos_close(h);
        if (rc == 0) return ((long)date << 16) | time;
    }
#endif
    return (long)st.st_mtime;
}

// Hash every plausible ROM file in `dir` (or take the hash from the cache); contents are not retained.
bool scan_folder(const char* dir, std::vector<Found>& found)
{
    DIR* d = opendir(dir);
    if (!d)
    {
        fail("Cannot open ROM folder '%s'.", dir);
        return false;
    }
    std::vector<CacheEntry> cache, now;
    bool changed = false;
    load_cache(dir, cache);
    while (dirent* e = readdir(d))
    {
        if (e->d_name[0] == '.') continue;
        if (!strcasecmp(e->d_name, kHashCache) || !strcasecmp(e->d_name, "ROMHASH.$$$")) continue;
        std::string path = join_path(dir, e->d_name);
        struct stat st;
        if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
        if (st.st_size < 512 || st.st_size > 16 * 1024 * 1024) continue;
        Found f;
        const long mtime = file_stamp(path, st);
        const CacheEntry* hit = nullptr;
        for (const CacheEntry& c : cache)
            if (!strcasecmp(c.name.c_str(), e->d_name) && c.size == (long)st.st_size && c.mtime == mtime) { hit = &c; break; }
        if (hit) f.digest = hit->digest;
        else
        {
            std::vector<uint8_t> bytes;
            if (!read_file(path, bytes)) continue;
            if (!SHA256_HashBytes(bytes, f.digest)) continue;
            changed = true;
        }
        f.path = path;
        now.push_back(CacheEntry{ e->d_name, (long)st.st_size, mtime, f.digest });
        found.push_back(std::move(f));
    }
    closedir(d);
    if (changed || now.size() != cache.size()) save_cache(dir, now);
    return true;
}

const Found* find_hash(const std::vector<Found>& found, const SHA256_Digest& h)
{
    for (const Found& f : found)
        if (f.digest == h) return &f;
    return nullptr;
}

// Counts how many of the definition's ROMs were found; optionally names the first missing one.
int completion(const RomsetDefinition& def, const std::vector<Found>& found, int* total, const char** first_missing)
{
    int have = 0, all = 0;
    if (first_missing) *first_missing = nullptr;
    for (auto loc : def.GetValidLocations())
    {
        all++;
        if (find_hash(found, def.GetHash(loc)))
            have++;
        else if (first_missing && !*first_missing)
            *first_missing = ToCString(loc);
    }
    if (total) *total = all;
    return have;
}

// Preference order for "auto": the five SC-55 firmwares VSC-55 exposes first, then mkII, then the rest of the family.
const char* const kPreferred[] = {"mk1-v2.00", "mk1-v1.21", "mk1-v1.20", "mk1-v1.10", "mk1-v1.00", "mk2-v1.01"};

std::vector<const RomsetDefinition*> candidate_order()
{
    std::vector<const RomsetDefinition*> order;
    auto defs = GetStandardRomsetDefinitions();
    for (const char* name : kPreferred)
        for (const RomsetDefinition& d : defs)
            if (ieq(d.name, name)) order.push_back(&d);
    for (const RomsetDefinition& d : defs)
    {
        if (!sc55_family(d.romset)) continue;
        bool seen = false;
        for (auto* p : order) seen |= (p == &d);
        if (!seen) order.push_back(&d);
    }
    return order;
}

} // namespace

extern "C" {

void SC55_ListModels(void (*cb)(const char*, void*), void* user)
{
    for (const RomsetDefinition* d : candidate_order())
        cb(d->name, user);
}

// Calls cb for every SC-55 family romset whose ROMs are all present in romdir (matched by SHA-256).
// Returns the number of complete sets, or -1 if the folder cannot be read.
int SC55_ListInstalledModels(const char* romdir, void (*cb)(const char*, void*), void* user)
{
    try
    {
        std::vector<Found> found;
        int n = 0;
        if (!romdir || !scan_folder(romdir, found)) return -1;
        for (const RomsetDefinition* d : candidate_order())
        {
            int total = 0;
            if (completion(*d, found, &total, nullptr) == total && total > 0)
            {
                cb(d->name, user);
                n++;
            }
        }
        return n;
    }
    catch (...)
    {
        return -1;
    }
}

// The core reports odd firmware behaviour (e.g. unknown register reads) through this hook.
// The default prints to stderr, which is unsafe inside the audio interrupt, so keep only
// the last warning/error in a static buffer (no allocation, no I/O).
static char g_diag[160];
static void silent_diag(Diag_Category cat, std::string_view msg)
{
    if (cat == Diag_Category::Debug)
        return;
    size_t n = msg.size() < sizeof(g_diag) - 1 ? msg.size() : sizeof(g_diag) - 1;
    memcpy(g_diag, msg.data(), n);
    g_diag[n] = '\0';
}

extern "C" const char* SC55_LastDiagnostic(void) { return g_diag; }

int SC55_Init(const SC55_Config* cfg)
{
    g_err[0] = '\0';
    Diag_SetCallback(&silent_diag);
    try
    {
        if (g_engine)
        {
            fail("SC-55 is already running.");
            return 0;
        }
        if (!cfg || !cfg->romdir || !*cfg->romdir)
        {
            fail("No ROM folder given.");
            return 0;
        }
        const bool verbose = cfg->verbose != 0;

        if (verbose) printf("Scanning ROM folder %s ...\n", cfg->romdir);
        std::vector<Found> found;
        if (!scan_folder(cfg->romdir, found)) return 0;
        if (verbose) printf("  %u candidate file(s) hashed.\n", (unsigned)found.size());

        // ---- choose the romset -------------------------------------------------
        const RomsetDefinition* def = nullptr;
        const bool automatic = !cfg->romset || !*cfg->romset || ieq(cfg->romset, "auto");
        if (automatic)
        {
            for (const RomsetDefinition* d : candidate_order())
            {
                int total = 0;
                if (completion(*d, found, &total, nullptr) == total)
                {
                    def = d;
                    break;
                }
            }
            if (!def)
            {
                // Describe the closest miss to make the error actionable.
                const RomsetDefinition* best = nullptr;
                int best_have = -1, best_total = 0;
                const char* best_missing = nullptr;
                for (const RomsetDefinition* d : candidate_order())
                {
                    int total = 0;
                    const char* miss = nullptr;
                    int have = completion(*d, found, &total, &miss);
                    if (have > best_have)
                    {
                        best = d;
                        best_have = have;
                        best_total = total;
                        best_missing = miss;
                    }
                }
                if (best_have > 0)
                    fail("No complete SC-55 ROM set in '%s'. Closest: %s has %d of %d ROMs (missing %s).",
                         cfg->romdir, best->name, best_have, best_total, best_missing ? best_missing : "?");
                else
                    fail("No SC-55 ROMs found in '%s' (%u file(s) checked by SHA-256).", cfg->romdir,
                         (unsigned)found.size());
                return 0;
            }
        }
        else
        {
            for (const RomsetDefinition& d : GetStandardRomsetDefinitions())
                if (ieq(d.name, cfg->romset)) def = &d;
            if (!def)
            {
                fail("Unknown ROM set '%s'. Use /VSCSET with one of the names printed by /?.", cfg->romset);
                return 0;
            }
            int total = 0;
            const char* miss = nullptr;
            int have = completion(*def, found, &total, &miss);
            if (have != total)
            {
                fail("ROM set %s is incomplete in '%s': %d of %d ROMs found (missing %s).", def->name,
                     cfg->romdir, have, total, miss ? miss : "?");
                return 0;
            }
        }
        snprintf(g_model, sizeof(g_model), "%s", def->name);
        if (verbose) printf("ROM set: %s\n", def->name);

        // ---- load the ROM contents (waveroms are unscrambled by LoadRomset) -----
        g_info = RomsetInfo{};
        for (auto loc : def->GetValidLocations())
        {
            const Found* f = find_hash(found, def->GetHash(loc));
            if (!f || !read_file(f->path, g_info.rom_data[(size_t)loc]))
            {
                fail("Could not read %s for %s.", ToCString(loc), def->name);
                return 0;
            }
        }
        if (!LoadRomset(g_info, nullptr))
        {
            fail("ROM set %s failed to load.", def->name);
            return 0;
        }

        // ---- start the emulator -----------------------------------------------
        g_core.emu = std::make_unique<Emulator>();
        EMU_Options opt;
        opt.lcd_backend = &g_null_lcd; // draws nothing; it only makes the core keep the LCD contents (SC55_GetLCD)
        if (!g_core.emu->Init(opt))
        {
            fail("Out of memory creating the emulator (it needs about 22 MB of extended memory).");
            g_core.emu.reset();
            return 0;
        }
        if (!g_core.emu->LoadRoms(def->romset, g_info))
        {
            fail("Emulator rejected ROM set %s.", def->name);
            g_core.emu.reset();
            return 0;
        }
        g_info.PurgeRomData(); // the core keeps its own copy; give the 5-6 MB back
        g_core.emu->Reset();

        const uint32_t native_rate = PCM_GetOutputFrequency(g_core.emu->GetPCM());
        const int dc_offset = g_core.emu->GetMCU().is_mk1 ? 512 : 0; // measured MK1 bias, see VSC-55 README
        g_engine = new (g_engine_storage) Eng(g_core, native_rate, dc_offset);
        g_engine->set_gain_percent(cfg->gain_percent); // 0 is a valid (muted) gain

        // ---- let the firmware boot -------------------------------------------
        const int boot = cfg->boot_seconds > 0 ? cfg->boot_seconds : 3; // measured: mk1 ready after ~2.3 s, mkII ~2 s
        if (verbose) printf("Booting SC-55 firmware (%d s of emulated time) ", boot);
        const uint32_t chunk = native_rate / 2;
        for (int i = 0; i < boot * 2; i++)
        {
            if (!g_engine->prime(chunk))
            {
                if (verbose) printf("\n");
                fail("The emulated firmware stopped producing audio while booting (bad or mismatched ROMs?).");
                SC55_Shutdown();
                return 0;
            }
            if (verbose)
            {
                putchar('.');
                fflush(stdout);
            }
        }
        if (verbose) printf(" done.\n");
        return 1;
    }
    catch (const std::bad_alloc&)
    {
        fail("Out of memory (the SC-55 emulation needs about 22 MB of extended memory).");
    }
    catch (const std::exception& e)
    {
        fail("Unexpected error: %s", e.what());
    }
    catch (...)
    {
        fail("Unexpected error while starting the SC-55 emulation.");
    }
    SC55_Shutdown();
    return 0;
}

void SC55_Shutdown(void)
{
    if (g_engine)
    {
        g_engine->~Eng();
        g_engine = nullptr;
    }
    g_core.emu.reset();
    g_info = RomsetInfo{};
}

int SC55_IsLoaded(void) { return g_engine != nullptr; }
const char* SC55_LastError(void) { return g_err; }
const char* SC55_ModelName(void) { return g_model; }
uint32_t SC55_NativeRate(void) { return g_engine ? g_engine->native_rate() : 0; }

int SC55_PostByte(uint8_t b) { return g_engine && g_engine->post_byte(b) ? 1 : 0; }
int SC55_Busy(void) { return g_engine && g_engine->busy() ? 1 : 0; }
int SC55_Queued(void) { return g_engine ? (int)g_engine->queued() : 0; }
extern "C++" { extern bool PCM_LiteFx; extern int PCM_MaxVoices; }
void SC55_SetLite(int fx_on, int max_voices)
{
    PCM_LiteFx = fx_on == 0;
    PCM_MaxVoices = max_voices;
}
void SC55_SetGain(int percent)
{
    if (g_engine) g_engine->set_gain_percent(percent);
}
void SC55_SetMixVolume(int q8)
{
    if (g_engine) g_engine->set_mix_volume_q8(q8);
}
/* the module's LCD as the firmware wrote it: 80 bytes of display RAM (line 1 at 0, line 2 at 40) and the
 * 64 bytes of custom characters (the 16-part level bars are made of them). Returns 1 if the display is on. */
int SC55_GetLCD(uint8_t* data80, uint8_t* cg64)
{
    if (!g_core.emu) return 0;
    lcd_t& l = g_core.emu->GetLCD();
    memcpy(data80, l.LCD_Data, 80);
    memcpy(cg64, l.LCD_CG, 64);
    return l.enable ? 1 : 0;
}

int SC55_Active(void) { return g_engine && g_engine->active() ? 1 : 0; }
void SC55_Render(int16_t* pcm, int frames, uint32_t out_rate, int domix)
{
    if (g_engine) g_engine->render(pcm, frames, out_rate, domix != 0);
}

} // extern "C"
