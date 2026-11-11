// install98.cpp - set up / remove NUKED SB-EMU in Windows 98.
//
// Install (SC55W.EXE /INSTALL, run from the pack's FILES\WIN98 folder):
//   - copies SC55W.EXE, SC55W.VXD, SC55MIDI.DRV, README98.TXT to <pack>\WIN98
//     and any ROMs that are missing to <pack>\ROMS (from FILES\ROMS), and the Xbox 360 pad
//     driver folder X360 (installed by the user through Windows, see README98)
//   - SC55MIDI.DRV to WINDOWS\SYSTEM, registered in SYSTEM.INI [drivers] and
//     under MediaResources\midi, so it is the MIDI device "NUKED SB-EMU"
//   - optional: start with Windows, default MIDI device (MIDI Mapper), turn
//     off SBEMUL (Windows 98 SE's own SB emulation for WDM sound drivers),
//     BLASTER= in AUTOEXEC.BAT
//   - an entry in Add/Remove Programs
// Everything that is changed is noted in WIN98\SC55W.INI [Install] so that
// uninstall puts it back. Settings (SBOPTS.TXT) and ROMs are never removed.
#include "install98.h"
#include "gameport.h"
#include "resource.h"
#include <cstdio>
#include <cstring>
#include <vector>

static const char TITLE[] = "NUKED SB-EMU for Windows 98 - Setup";
static const char WND_CLASS[] = "SC55W_Main";
static const char MIDI_NAME[] = "NUKED SB-EMU";
static const char OLD_MIDI_NAME[] = "SC-55 PACK";      /* before the rename: still recognised */
static const char RUN_KEY[] = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const char UNINST_KEY[] = "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\SC55W";
static const char MEDIA_KEY[] = "System\\CurrentControlSet\\Control\\MediaResources\\midi\\sc55midi.drv";
static const char MIDIMAP_KEY[] = "Software\\Microsoft\\Windows\\CurrentVersion\\Multimedia\\MIDIMap";
static const char SBEMUL_KEY[] = "System\\CurrentControlSet\\Services\\sbemul";
static const char MARK[] = "REM [SC55W] BLASTER for DOS programs (kept up to date by SC55W.EXE)";

// ---------------------------------------------------------------------------
static std::string join(const std::string& a, const std::string& b)
{
    if (a.empty()) return b;
    char c = a[a.size() - 1];
    return (c == '\\' || c == '/') ? a + b : a + "\\" + b;
}
static std::string parent(const std::string& p)
{
    size_t k = p.find_last_of("\\/");
    return k == std::string::npos ? p : p.substr(0, k);
}
static bool exists(const std::string& p) { return GetFileAttributesA(p.c_str()) != 0xFFFFFFFF; }
static bool is_dir(const std::string& p)
{
    DWORD a = GetFileAttributesA(p.c_str());
    return a != 0xFFFFFFFF && (a & FILE_ATTRIBUTE_DIRECTORY);
}
static std::string shortp(const std::string& p)
{
    char b[MAX_PATH];
    DWORD n = GetShortPathNameA(p.c_str(), b, sizeof b);
    return n && n < sizeof b ? std::string(b) : p;
}
static std::string upper(std::string s)
{
    for (auto& c : s) c = (char)toupper((unsigned char)c);
    return s;
}
static std::string exe_dir()
{
    char b[MAX_PATH];
    GetModuleFileNameA(NULL, b, sizeof b);
    return parent(b);
}
static std::string win_dir()
{
    char b[MAX_PATH];
    GetWindowsDirectoryA(b, sizeof b);
    return b;
}
static std::string sys_dir()
{
    char b[MAX_PATH];
    GetSystemDirectoryA(b, sizeof b);
    return b;
}
static std::string boot_root() { return win_dir().substr(0, 3); }   // "C:\"

static void reg_set_str(HKEY root, const char* key, const char* name, const std::string& v)
{
    HKEY k;
    if (RegCreateKeyExA(root, key, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL) != ERROR_SUCCESS) return;
    RegSetValueExA(k, name, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)v.size() + 1);
    RegCloseKey(k);
}
static void reg_set_dword(HKEY root, const char* key, const char* name, DWORD v)
{
    HKEY k;
    if (RegCreateKeyExA(root, key, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL) != ERROR_SUCCESS) return;
    RegSetValueExA(k, name, 0, REG_DWORD, (const BYTE*)&v, 4);
    RegCloseKey(k);
}
static bool reg_get_str(HKEY root, const char* key, const char* name, std::string& out)
{
    HKEY k;
    if (RegOpenKeyExA(root, key, 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    char b[512];
    DWORD sz = sizeof b, type = 0;
    bool ok = RegQueryValueExA(k, name, NULL, &type, (BYTE*)b, &sz) == ERROR_SUCCESS && type == REG_SZ;
    if (ok) out.assign(b, sz && b[sz - 1] == 0 ? sz - 1 : sz);
    RegCloseKey(k);
    return ok;
}
static bool reg_get_dword(HKEY root, const char* key, const char* name, DWORD& out)
{
    HKEY k;
    if (RegOpenKeyExA(root, key, 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    DWORD sz = 4, type = 0;
    bool ok = RegQueryValueExA(k, name, NULL, &type, (BYTE*)&out, &sz) == ERROR_SUCCESS && type == REG_DWORD;
    RegCloseKey(k);
    return ok;
}
static void reg_del_value(HKEY root, const char* key, const char* name)
{
    HKEY k;
    if (RegOpenKeyExA(root, key, 0, KEY_WRITE, &k) != ERROR_SUCCESS) return;
    RegDeleteValueA(k, name);
    RegCloseKey(k);
}

// a file that is in use (a loaded driver) is replaced at the next start via WININIT.INI
static void wininit_line(const std::string& line)
{
    std::string p = join(win_dir(), "WININIT.INI"), t;
    FILE* f = fopen(p.c_str(), "rb");
    if (f) {
        char b[4096];
        size_t n;
        while ((n = fread(b, 1, sizeof b, f)) > 0) t.append(b, n);
        fclose(f);
    }
    // a pending "dst=tmp" for the same file is dropped (and its temp file
    // deleted) so that a later "NUL=dst" (uninstall) is not undone by it
    size_t eq = line.find('=');
    if (eq != std::string::npos) {
        std::string dst = upper(line.substr(eq + 1)), out;
        if (line.compare(0, 4, "NUL=") == 0) {
            size_t s = 0;
            while (s < t.size()) {
                size_t e = t.find('\n', s);
                std::string l = t.substr(s, e == std::string::npos ? std::string::npos : e + 1 - s);
                std::string lu = upper(l);
                while (!lu.empty() && (lu.back() == '\n' || lu.back() == '\r')) lu.pop_back();
                size_t q = lu.find('=');
                if (q != std::string::npos && lu.substr(0, q) == dst) {
                    std::string tmp = l.substr(q + 1);
                    while (!tmp.empty() && (tmp.back() == '\n' || tmp.back() == '\r')) tmp.pop_back();
                    DeleteFileA(tmp.c_str());
                } else {
                    out += l;
                }
                if (e == std::string::npos) break;
                s = e + 1;
            }
            t = out;
        }
    }
    std::string u = upper(t);
    size_t k = u.find("[RENAME]");
    if (k == std::string::npos) {
        if (!t.empty() && t[t.size() - 1] != '\n') t += "\r\n";
        t += "[rename]\r\n" + line + "\r\n";
    } else {
        size_t e = t.find('\n', k);
        e = e == std::string::npos ? t.size() : e + 1;
        t.insert(e, line + "\r\n");
    }
    f = fopen(p.c_str(), "wb");
    if (f) { fwrite(t.data(), 1, t.size(), f); fclose(f); }
}

// copy a folder tree (the bundled X360 driver) next to the program; existing files are replaced
static int copy_tree(const std::string& src, const std::string& dst)
{
    int n = 0;
    CreateDirectoryA(dst.c_str(), NULL);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(join(src, "*.*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        std::string f = fd.cFileName;
        if (f == "." || f == "..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) n += copy_tree(join(src, f), join(dst, f));
        else if (CopyFileA(join(src, f).c_str(), join(dst, f).c_str(), FALSE)) {
            SetFileAttributesA(join(dst, f).c_str(), FILE_ATTRIBUTE_NORMAL);
            n++;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return n;
}

// delete a folder tree; what cannot go now goes at the next start
static void delete_tree(const std::string& dir)
{
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(join(dir, "*.*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::string f = fd.cFileName;
            if (f == "." || f == "..") continue;
            std::string p = join(dir, f);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) delete_tree(p);
            else {
                SetFileAttributesA(p.c_str(), FILE_ATTRIBUTE_NORMAL);
                if (!DeleteFileA(p.c_str())) wininit_line("NUL=" + shortp(p));
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryA(dir.c_str());
}

static bool copy_file(const std::string& src, const std::string& dst, std::string& log)
{
    if (upper(src) == upper(dst)) return true;
    // files copied from a CD are read-only: clear that, before and after
    if (exists(dst)) SetFileAttributesA(dst.c_str(), FILE_ATTRIBUTE_NORMAL);
    if (CopyFileA(src.c_str(), dst.c_str(), FALSE)) {
        SetFileAttributesA(dst.c_str(), FILE_ATTRIBUTE_NORMAL);
        return true;
    }
    // in use: copy next to it and swap at the next start
    char tb[MAX_PATH];
    if (!GetTempFileNameA(parent(dst).c_str(), "S55", 0, tb)) tb[0] = 0;
    std::string tmp = tb;
    if (tb[0] && CopyFileA(src.c_str(), tmp.c_str(), FALSE)) {
        SetFileAttributesA(tmp.c_str(), FILE_ATTRIBUTE_NORMAL);
        wininit_line(shortp(dst) + "=" + shortp(tmp));
        log += "  " + dst + " is in use; it is replaced when Windows restarts.\r\n";
        return true;
    }
    if (tb[0]) DeleteFileA(tb);
    log += "  could not copy " + src + " to " + dst + "\r\n";
    return false;
}

void stop_running_helper()
{
    HWND w = FindWindowA(WND_CLASS, NULL);
    if (!w) return;
    PostMessageA(w, WM_APP + 3, 3, 0);
    for (int i = 0; i < 100 && FindWindowA(WND_CLASS, NULL); i++) Sleep(100);
    Sleep(300);
}

// ---------------------------------------------------------------------------
// BLASTER / AUTOEXEC.BAT
std::string blaster_for(const Options& o)
{
    // the same line SBEMU sets: T is SBEMU's own type number (5 is written as 4)
    char b[80];
    int t = o.sbtype == 5 ? 4 : o.sbtype >= 1 && o.sbtype <= 6 ? o.sbtype : 4;
    snprintf(b, sizeof b, "A%X I%d D%d", o.sb_port, o.irq, o.dma8);
    std::string s = b;
    if (o.sbtype >= 6) { snprintf(b, sizeof b, " H%d", o.dma16); s += b; }
    if (o.vmpu) { snprintf(b, sizeof b, " P%X", o.mpu_port); s += b; }
    snprintf(b, sizeof b, " T%d", t);
    return s + b;
}

static bool read_lines(const std::string& p, std::vector<std::string>& lines)
{
    FILE* f = fopen(p.c_str(), "rb");
    lines.clear();
    if (!f) return false;
    std::string t;
    char b[4096];
    size_t n;
    while ((n = fread(b, 1, sizeof b, f)) > 0) t.append(b, n);
    fclose(f);
    size_t s = 0;
    while (s < t.size()) {
        size_t e = t.find('\n', s);
        std::string l = t.substr(s, e == std::string::npos ? std::string::npos : e - s);
        if (!l.empty() && l[l.size() - 1] == '\r') l.erase(l.size() - 1);
        if (!l.empty() && l[0] == 0x1A) break;         // ^Z
        lines.push_back(l);
        if (e == std::string::npos) break;
        s = e + 1;
    }
    return true;
}
static bool write_lines(const std::string& p, const std::vector<std::string>& lines)
{
    FILE* f = fopen(p.c_str(), "wb");
    if (!f) return false;
    for (auto& l : lines) fprintf(f, "%s\r\n", l.c_str());
    fclose(f);
    return true;
}

static int find_mark(const std::vector<std::string>& lines)
{
    for (size_t i = 0; i < lines.size(); i++)
        if (upper(lines[i]).find("REM [SC55W]") == 0) return (int)i;
    return -1;
}

bool autoexec_update_blaster(const Options& o)
{
    std::string p = boot_root() + "AUTOEXEC.BAT";
    std::vector<std::string> lines;
    if (!read_lines(p, lines)) return false;
    int m = find_mark(lines);
    if (m < 0) return false;
    std::string want = "SET BLASTER=" + blaster_for(o);
    if (m + 1 < (int)lines.size() && upper(lines[m + 1]).find("SET BLASTER=") == 0) {
        if (lines[m + 1] == want) return false;
        lines[m + 1] = want;
    } else {
        lines.insert(lines.begin() + m + 1, want);
    }
    return write_lines(p, lines);
}

static void autoexec_remove()
{
    std::string p = boot_root() + "AUTOEXEC.BAT";
    std::vector<std::string> lines, out;
    if (!read_lines(p, lines)) return;
    bool changed = false;
    for (size_t i = 0; i < lines.size(); i++) {
        std::string u = upper(lines[i]);
        if (u.find("REM [SC55W]") == 0) {
            changed = true;
            if (i + 1 < lines.size() && upper(lines[i + 1]).find("SET BLASTER=") == 0) i++;
            continue;
        }
        if (u.find("REM [SC55W-OLD] ") == 0) {
            out.push_back(lines[i].substr(16));
            changed = true;
            continue;
        }
        out.push_back(lines[i]);
    }
    if (changed) write_lines(p, out);
}

// The BLASTER value of the user's own SET BLASTER line in AUTOEXEC.BAT ("" if none)
static std::string autoexec_user_blaster()
{
    std::vector<std::string> lines;
    read_lines(boot_root() + "AUTOEXEC.BAT", lines);
    for (size_t i = 0; i < lines.size(); i++) {
        std::string u = upper(lines[i]);
        size_t k = u.find_first_not_of(" \t@");
        if (k == std::string::npos || u.compare(k, 12, "SET BLASTER=")) continue;
        if (i > 0 && upper(lines[i - 1]).find("REM [SC55W]") == 0) continue;   // ours
        return lines[i].substr(k + 12);
    }
    return "";
}

// Add our BLASTER line, but only if AUTOEXEC.BAT has none of its own: SBEMU takes
// the port / IRQ / DMA the user's games were set up for from that line, so it
// is never replaced. (Version 1 replaced it, which made games freeze: put it back.)
static void autoexec_add(const Options& o)
{
    autoexec_remove();                       // undo version 1 (restores a REM [SC55W-OLD] line)
    if (!autoexec_user_blaster().empty()) return;
    std::string p = boot_root() + "AUTOEXEC.BAT";
    std::vector<std::string> lines;
    read_lines(p, lines);
    lines.push_back(MARK);
    lines.push_back("SET BLASTER=" + blaster_for(o));
    write_lines(p, lines);
}

// ---------------------------------------------------------------------------
// MIDI driver registration
static std::string midi_key_in_use()
{
    char key[16], val[MAX_PATH];
    for (int i = 0; i < 10; i++) {
        if (i) snprintf(key, sizeof key, "midi%d", i);
        else strcpy(key, "midi");
        GetPrivateProfileStringA("drivers", key, "", val, sizeof val, "SYSTEM.INI");
        if (upper(val).find("SC55MIDI.DRV") != std::string::npos) return key;
    }
    return "";
}

static bool midi_register(std::string& log)
{
    if (!midi_key_in_use().empty()) return true;
    char key[16], val[MAX_PATH];
    for (int i = 1; i < 10; i++) {       // "midi" itself is left to the sound card
        snprintf(key, sizeof key, "midi%d", i);
        GetPrivateProfileStringA("drivers", key, "", val, sizeof val, "SYSTEM.INI");
        if (!val[0]) {
            WritePrivateProfileStringA("drivers", key, "sc55midi.drv", "SYSTEM.INI");
            return true;
        }
    }
    log += "  SYSTEM.INI [drivers] has no free midi1..midi9 entry; the MIDI device was not added.\r\n";
    return false;
}

static void midi_unregister()
{
    std::string k;
    while (!(k = midi_key_in_use()).empty()) WritePrivateProfileStringA("drivers", k.c_str(), NULL, "SYSTEM.INI");
    RegDeleteKeyA(HKEY_LOCAL_MACHINE, MEDIA_KEY);
}

// ---------------------------------------------------------------------------
static std::string g_dir;

static INT_PTR CALLBACK install_proc(HWND d, UINT m, WPARAM w, LPARAM)
{
    switch (m) {
    case WM_INITDIALOG:
        SetDlgItemTextA(d, IDC_I_DIR, g_dir.c_str());
        CheckDlgButton(d, IDC_I_AUTO, BST_CHECKED);
        CheckDlgButton(d, IDC_I_MAP, BST_CHECKED);
        CheckDlgButton(d, IDC_I_SBEMUL, BST_CHECKED);
        CheckDlgButton(d, IDC_I_BLASTER, BST_CHECKED);
        DWORD start;
        if (!reg_get_dword(HKEY_LOCAL_MACHINE, SBEMUL_KEY, "Start", start)) {   // no WDM SB emulation here
            CheckDlgButton(d, IDC_I_SBEMUL, BST_UNCHECKED);
            EnableWindow(GetDlgItem(d, IDC_I_SBEMUL), FALSE);
        }
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(w) == IDOK) {
            char b[MAX_PATH];
            GetDlgItemTextA(d, IDC_I_DIR, b, sizeof b);
            g_dir = b;
            while (g_dir.size() > 3 && (g_dir.back() == '\\' || g_dir.back() == '/')) g_dir.pop_back();
            int f = 0;
            if (IsDlgButtonChecked(d, IDC_I_AUTO)) f |= 1;
            if (IsDlgButtonChecked(d, IDC_I_MAP)) f |= 2;
            if (IsDlgButtonChecked(d, IDC_I_SBEMUL)) f |= 4;
            if (IsDlgButtonChecked(d, IDC_I_BLASTER)) f |= 8;
            EndDialog(d, 0x100 | f);
            return TRUE;
        }
        if (LOWORD(w) == IDCANCEL) { EndDialog(d, 0); return TRUE; }
        return FALSE;
    }
    return FALSE;
}

int run_install(HINSTANCE inst)
{
    std::string src = exe_dir();
    g_dir = is_dir("C:\\SC55") ? "C:\\SC55" : boot_root() + "SC55";
    INT_PTR r = DialogBoxParamA(inst, MAKEINTRESOURCEA(IDD_INSTALL), NULL, install_proc, 0);
    if (!(r & 0x100)) return 1;
    int flags = (int)r & 0xFF;

    stop_running_helper();
    std::string log;
    std::string base = g_dir, wdir = join(base, "WIN98"), rdir = join(base, "ROMS");
    CreateDirectoryA(base.c_str(), NULL);
    CreateDirectoryA(wdir.c_str(), NULL);
    CreateDirectoryA(rdir.c_str(), NULL);
    CreateDirectoryA(join(base, "SF2").c_str(), NULL);
    if (!is_dir(wdir)) {
        MessageBoxA(NULL, ("Cannot create " + wdir).c_str(), TITLE, MB_ICONSTOP);
        return 1;
    }
    std::string ini = join(wdir, "SC55W.INI");

    // program files
    const char* files[] = {"SC55W.EXE", "SC55WM.EXE", "SC55W2.EXE", "SC55W.VXD", "SC55MIDI.DRV", "README98.TXT"};
    bool ok = true;
    for (const char* f : files) {
        std::string s = join(src, f);
        if (!exists(s)) {
            if (strcmp(f, "README98.TXT")) { log += std::string("  ") + f + " is missing next to SETUP.\r\n"; ok = false; }
            continue;
        }
        ok &= copy_file(s, join(wdir, f), log);
    }
    // the Xbox 360 pad driver, so it is on the hard disk when the pad is first plugged in
    if (is_dir(join(src, "X360"))) copy_tree(join(src, "X360"), join(wdir, "X360"));
    if (is_dir(join(src, "JOY"))) copy_tree(join(src, "JOY"), join(wdir, "JOY"));   // NUKED game port + joystick
    // the user's game pad layout download (FILES\PADMAP\keyb2joypad.cpp, see DOWNLOADS.TXT):
    // SC55W.EXE turns it into PADMAP.TXT at its next start
    if (is_dir(join(parent(src), "PADMAP"))) copy_tree(join(parent(src), "PADMAP"), join(base, "PADMAP"));
    // ROMs from the pack's FILES\ROMS (only the missing ones)
    int roms = 0;
    std::string rsrc = join(parent(src), "ROMS");
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(join(rsrc, "*.*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::string dst = join(rdir, fd.cFileName);
            if (exists(dst)) continue;
            if (CopyFileA(join(rsrc, fd.cFileName).c_str(), dst.c_str(), TRUE)) {
                SetFileAttributesA(dst.c_str(), FILE_ATTRIBUTE_NORMAL);
                roms++;
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    // MIDI driver
    if (exists(join(src, "SC55MIDI.DRV"))) {
        copy_file(join(src, "SC55MIDI.DRV"), join(sys_dir(), "SC55MIDI.DRV"), log);
        midi_register(log);
        reg_set_str(HKEY_LOCAL_MACHINE, MEDIA_KEY, "Active", "1");
        reg_set_str(HKEY_LOCAL_MACHINE, MEDIA_KEY, "Description", MIDI_NAME);
        reg_set_str(HKEY_LOCAL_MACHINE, MEDIA_KEY, "Driver", "sc55midi.drv");
        reg_set_str(HKEY_LOCAL_MACHINE, MEDIA_KEY, "FriendlyName", MIDI_NAME);
    }
    std::string exe = shortp(join(wdir, "SC55W.EXE"));
    if (flags & 2) {
        std::string old;
        DWORD scheme = 0;
        if (reg_get_str(HKEY_CURRENT_USER, MIDIMAP_KEY, "CurrentInstrument", old) && old != MIDI_NAME &&
            GetPrivateProfileIntA("Install", "MapSaved", 0, ini.c_str()) == 0) {
            WritePrivateProfileStringA("Install", "OldInstrument", old.c_str(), ini.c_str());
            if (reg_get_dword(HKEY_CURRENT_USER, MIDIMAP_KEY, "UseScheme", scheme))
                WritePrivateProfileStringA("Install", "OldUseScheme", scheme ? "1" : "0", ini.c_str());
            WritePrivateProfileStringA("Install", "MapSaved", "1", ini.c_str());
        }
        reg_set_str(HKEY_CURRENT_USER, MIDIMAP_KEY, "CurrentInstrument", MIDI_NAME);
        reg_set_dword(HKEY_CURRENT_USER, MIDIMAP_KEY, "UseScheme", 0);
    }
    if (flags & 1) reg_set_str(HKEY_LOCAL_MACHINE, RUN_KEY, "SC55W", exe + " /BOOT");
    else reg_del_value(HKEY_LOCAL_MACHINE, RUN_KEY, "SC55W");
    if (flags & 4) {
        DWORD start = 0;
        if (reg_get_dword(HKEY_LOCAL_MACHINE, SBEMUL_KEY, "Start", start) && start != 4) {
            char b[16];
            snprintf(b, sizeof b, "%lu", start);
            WritePrivateProfileStringA("Install", "SbemulStart", b, ini.c_str());
            reg_set_dword(HKEY_LOCAL_MACHINE, SBEMUL_KEY, "Start", 4);
        }
    }
    // Settings: like SBEMU, what SBOPTS.TXT does not say comes from the user's own
    // BLASTER line. Version 1 replaced that line, so restore it first and read it
    // from the file (this program's environment may still hold the replaced one).
    autoexec_remove();
    std::string ub = autoexec_user_blaster();
    Options o;
    o.apply_blaster(ub.empty() ? getenv("BLASTER") : ub.c_str());
    {
        std::string t;
        std::string op = join(base, "SBOPTS.TXT");
        FILE* f = fopen(op.c_str(), "rb");
        if (f) {
            char b[4096];
            size_t n;
            while ((n = fread(b, 1, sizeof b, f)) > 0) t.append(b, n);
            fclose(f);
            o.parse(t);
        }
        // write port / IRQ / DMA out in full, so DOS and Windows use the same ones
        // whatever BLASTER says later
        f = fopen(op.c_str(), "wb");
        std::string l = o.format();
        if (f) { fwrite(l.data(), 1, l.size(), f); fclose(f); }
        f = fopen(join(base, "SBOPTS.BAT").c_str(), "wb");
        if (f) { fprintf(f, "@ECHO OFF\r\nSET SBOPTS=%s\r\n", l.c_str()); fclose(f); }
    }
    if (flags & 8) autoexec_add(o);
    reg_set_str(HKEY_LOCAL_MACHINE, UNINST_KEY, "DisplayName", "NUKED SB-EMU for Windows 98");
    reg_set_str(HKEY_LOCAL_MACHINE, UNINST_KEY, "UninstallString", exe + " /UNINSTALL");
    WritePrivateProfileStringA("Install", "Installed", "1", ini.c_str());

    std::string msg = ok ? "The NUKED SB-EMU for Windows 98 is installed in " + wdir + ".\r\n"
                         : "Setup finished with problems:\r\n" + log + "\r\n";
    if (ok && !log.empty()) msg += "\r\n" + log;
    if (roms) {
        char b[80];
        snprintf(b, sizeof b, "%d ROM files copied to ", roms);
        msg += std::string("\r\n") + b + rdir + ".\r\n";
    }
    msg += "\r\nDOS programs: Sound Blaster " + blaster_for(o) +
           " (change it in Settings; it is the DOS pack's SBOPTS.TXT).\r\n"
           "Windows programs: MIDI device \"NUKED SB-EMU\".\r\n\r\n"
           "Windows must be restarted to load the MIDI driver. Restart now?";
    if (MessageBoxA(NULL, msg.c_str(), TITLE, MB_YESNO | MB_ICONINFORMATION) == IDYES)
        ExitWindowsEx(EWX_REBOOT, 0);
    return ok ? 0 : 1;
}

int run_uninstall(HINSTANCE)
{
    if (MessageBoxA(NULL, "Remove the NUKED SB-EMU from Windows?\r\n\r\nYour settings (SBOPTS.TXT), the ROMs and the "
                    "DOS pack stay in place.", TITLE, MB_YESNO | MB_ICONQUESTION) != IDYES) return 1;
    stop_running_helper();
    std::string wdir = exe_dir(), ini = join(wdir, "SC55W.INI");
    reg_del_value(HKEY_LOCAL_MACHINE, RUN_KEY, "SC55W");
    midi_unregister();
    std::string cur;
    if (reg_get_str(HKEY_CURRENT_USER, MIDIMAP_KEY, "CurrentInstrument", cur) && (cur == MIDI_NAME || cur == OLD_MIDI_NAME)) {
        char old[256];
        GetPrivateProfileStringA("Install", "OldInstrument", "", old, sizeof old, ini.c_str());
        if (old[0]) reg_set_str(HKEY_CURRENT_USER, MIDIMAP_KEY, "CurrentInstrument", old);
        else reg_del_value(HKEY_CURRENT_USER, MIDIMAP_KEY, "CurrentInstrument");
        int sch = GetPrivateProfileIntA("Install", "OldUseScheme", -1, ini.c_str());
        if (sch >= 0) reg_set_dword(HKEY_CURRENT_USER, MIDIMAP_KEY, "UseScheme", (DWORD)sch);
    }
    int sb = GetPrivateProfileIntA("Install", "SbemulStart", -1, ini.c_str());
    if (sb >= 0) reg_set_dword(HKEY_LOCAL_MACHINE, SBEMUL_KEY, "Start", (DWORD)sb);
    autoexec_remove();
    RegDeleteKeyA(HKEY_LOCAL_MACHINE, UNINST_KEY);
    // files in use (the driver, this program) go at the next start
    SetFileAttributesA(join(sys_dir(), "SC55MIDI.DRV").c_str(), FILE_ATTRIBUTE_NORMAL);
    wininit_line("NUL=" + shortp(join(sys_dir(), "SC55MIDI.DRV")));
    const char* files[] = {"SC55W.EXE", "SC55WM.EXE", "SC55W2.EXE", "SC55W.VXD", "SC55MIDI.DRV", "SC55W.INI", "SC55W.LOG", "README98.TXT"};
    for (const char* f : files) {
        std::string p = join(wdir, f);
        if (!exists(p)) continue;
        SetFileAttributesA(p.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (!DeleteFileA(p.c_str())) wininit_line("NUL=" + shortp(p));
    }
    delete_tree(join(wdir, "X360"));
    {
        std::string gl;
        gameport_remove(gl);                     // NUKED game port + joystick, if set up
        delete_tree(join(wdir, "JOY"));
    }
    MessageBoxA(NULL, ("The NUKED SB-EMU has been removed from Windows. The last files are deleted when Windows "
                       "restarts; the empty folder " + wdir + " can then be deleted.").c_str(), TITLE, MB_ICONINFORMATION);
    return 0;
}
