// romscan.cpp - identify ROM dumps by SHA-256 (SC-55, via Nuked-SC55's
// romset table) and by Munt's ROM database (MT-32 / CM-32L).
// Plain ANSI file APIs only, so it runs on Windows 98.
#include "romscan.h"
#include "rom.h"
#include "standard_romsets.h"
#include "sha/sha.h"
#include "mt32emu.h"
#include <cstdio>
#include <cstring>
#include <cctype>
#include <map>
#include <memory>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

std::string path_join(const std::string& dir, const std::string& name)
{
    if (dir.empty()) return name;
    char last = dir.back();
#ifdef _WIN32
    if (last == '\\' || last == '/' || last == ':') return dir + name;
    return dir + "\\" + name;
#else
    if (last == '/') return dir + name;
    return dir + "/" + name;
#endif
}

struct DirEntry { std::string name; unsigned long size, mtime; };

static std::vector<DirEntry> list_dir(const std::string& dir)
{
    std::vector<DirEntry> v;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(path_join(dir, "*.*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return v;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        ULARGE_INTEGER t; t.LowPart = fd.ftLastWriteTime.dwLowDateTime; t.HighPart = fd.ftLastWriteTime.dwHighDateTime;
        v.push_back({fd.cFileName, fd.nFileSizeLow, (unsigned long)(t.QuadPart / 10000000ULL)});
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(dir.c_str());
    if (!d) return v;
    while (dirent* e = readdir(d)) {
        struct stat st;
        std::string p = path_join(dir, e->d_name);
        if (stat(p.c_str(), &st) || !S_ISREG(st.st_mode)) continue;
        v.push_back({e->d_name, (unsigned long)st.st_size, (unsigned long)st.st_mtime});
    }
    closedir(d);
#endif
    return v;
}

static std::string hash_file(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return "";
    SHA256Context ctx;
    SHA256Reset(&ctx);
    static uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) SHA256Input(&ctx, buf, (unsigned)n);
    fclose(f);
    uint8_t dig[32];
    SHA256Result(&ctx, dig);
    char hex[65];
    for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", dig[i]);
    return hex;
}

static bool is_cache_or_text(const std::string& n)
{
    std::string u;
    for (char c : n) u += (char)toupper((unsigned char)c);
    return u == "SC55W.CAC" || u == "ROMHASH.CAC" || (u.size() > 4 && u.substr(u.size() - 4) == ".TXT");
}

std::vector<RomFile> scan_roms(const std::string& dir)
{
    std::map<std::string, RomFile> cache;
    std::string cpath = path_join(dir, "SC55W.CAC");
    if (FILE* f = fopen(cpath.c_str(), "r")) {
        char name[260], hex[80];
        unsigned long size, mtime;
        while (fscanf(f, "%259s %lu %lu %79s", name, &size, &mtime, hex) == 4)
            cache[name] = RomFile{name, size, mtime, hex};
        fclose(f);
    }
    std::vector<RomFile> out;
    bool dirty = false;
    for (auto& e : list_dir(dir)) {
        if (is_cache_or_text(e.name) || e.size < 4096 || e.size > 16u * 1024 * 1024) continue;
        auto it = cache.find(e.name);
        if (it != cache.end() && it->second.size == e.size && it->second.mtime == e.mtime) {
            out.push_back(it->second);
            continue;
        }
        RomFile r{e.name, e.size, e.mtime, hash_file(path_join(dir, e.name))};
        if (r.sha256.empty()) continue;
        out.push_back(r);
        dirty = true;
    }
    if (dirty || out.size() != cache.size()) {
        if (FILE* f = fopen(cpath.c_str(), "w")) {
            for (auto& r : out) fprintf(f, "%s %lu %lu %s\n", r.name.c_str(), r.size, r.mtime, r.sha256.c_str());
            fclose(f);
        }
    }
    return out;
}

static std::string digest_hex(const SHA256_Digest& d)
{
    char hex[65];
    for (int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", d[i]);
    return hex;
}

bool find_sc55(const std::string& dir, const std::vector<RomFile>& files, const std::string& wanted,
               Sc55Choice& out, std::vector<std::string>& complete, std::string& msg)
{
    std::map<std::string, std::string> by_hash;
    for (auto& f : files) by_hash[f.sha256] = f.name;

    std::string want;
    for (char c : wanted) want += (char)tolower((unsigned char)c);
    if (want.empty()) want = "auto";

    std::vector<Sc55Choice> found;
    for (const RomsetDefinition& def : GetStandardRomsetDefinitions()) {
        Sc55Choice c;
        c.set_name = def.name;
        c.romset = (int)def.romset;
        bool ok = true;
        for (size_t i = 0; i < ROMLOCATION_COUNT; i++) {
            RomLocation loc = (RomLocation)i;
            if (!def.ContainsHash(loc)) continue;
            auto it = by_hash.find(digest_hex(def.GetHash(loc)));
            if (it != by_hash.end()) c.paths[i] = path_join(dir, it->second);
            else if (IsRequiredRom(def.romset, loc)) { ok = false; break; }
        }
        if (ok) { found.push_back(c); complete.push_back(c.set_name); }
    }
    if (found.empty()) { msg = "No complete SC-55 ROM set in " + dir; return false; }

    if (want == "auto") {
        // same preference order as the DOS pack's automatic setup
        static const char* pref[] = {"mk1-v1.21", "mk2-v1.01", "mk1-v2.00", "mk1-v1.20", "mk1-v1.10", "mk1-v1.00", "st-v1.01"};
        for (const char* p : pref)
            for (auto& c : found)
                if (c.set_name == p) { out = c; return true; }
        out = found[0];
        return true;
    }
    for (auto& c : found)
        if (c.set_name == want) { out = c; return true; }
    msg = "ROM set \"" + wanted + "\" not found (complete sets: ";
    for (size_t i = 0; i < complete.size(); i++) msg += (i ? ", " : "") + complete[i];
    msg += ")";
    return false;
}

bool find_mt32(const std::string& dir, const std::vector<RomFile>& files, const std::string& wanted,
               Mt32Choice& out, std::string& msg)
{
    using namespace MT32Emu;
    struct Found { std::string path, desc; bool control, cm32l; };
    std::vector<Found> roms;
    for (auto& f : files) {
        if (f.size != 32768 && f.size != 65536 && f.size != 131072 && f.size != 524288 && f.size != 1048576) continue;
        FileStream fs;
        std::string p = path_join(dir, f.name);
        if (!fs.open(p.c_str())) continue;
        const ROMInfo* ri = ROMInfo::getROMInfo(&fs);
        if (!ri || ri->pairType != ROMInfo::Full) { if (ri) ROMInfo::freeROMInfo(ri); continue; }
        std::string sn = ri->shortName;
        bool cm = sn.find("cm32l") != std::string::npos || sn.find("cm32ln") != std::string::npos;
        roms.push_back({p, ri->description, ri->type == ROMInfo::Control, cm});
        ROMInfo::freeROMInfo(ri);
    }
    std::string w = wanted.empty() ? "AUTO" : wanted;
    auto pick = [&](bool cm) {
        const Found *ctl = nullptr, *pcm = nullptr;
        for (auto& r : roms) {
            if (r.cm32l != cm) continue;
            // MT-32: prefer the original 1.0x firmware games were written for, newest of those
            auto rank = [](const std::string& d) { return (d.find("v1.") != std::string::npos ? 100 : 0) + (int)(d.empty() ? 0 : d.back()); };
            if (r.control && (!ctl || rank(r.desc) > rank(ctl->desc))) ctl = &r;
            if (!r.control && !pcm) pcm = &r;
        }
        if (!ctl || !pcm) return false;
        out.control = ctl->path; out.pcm = pcm->path; out.control_desc = ctl->desc; out.cm32l = cm;
        return true;
    };
    if (w == "MT32" && pick(false)) return true;
    if (w == "CM32L" && pick(true)) return true;
    if (w == "AUTO" && (pick(false) || pick(true))) return true;
    msg = "No " + (w == "AUTO" ? std::string("MT-32 or CM-32L") : w) + " control + PCM ROM pair in " + dir;
    return false;
}
