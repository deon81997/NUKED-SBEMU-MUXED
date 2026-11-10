// gameport.cpp - see gameport.h
#include "gameport.h"
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <vector>

static const char HWID[] = "*NUK0201";
static const char CLASSGUID[] = "{4d36e96c-e325-11ce-bfc1-08002be10318}";
static const char DESC[] = "NUKED SB-EMU Game Port";
static const char ENUMROOT[] = "Enum\\Root\\MEDIA";
static const char CLASSKEY[] = "System\\CurrentControlSet\\Services\\Class\\MEDIA";
static const char OEMKEY[] = "System\\CurrentControlSet\\Control\\MediaProperties\\PrivateProperties\\Joystick\\OEM\\NUKED_SBEMU";
static const char CFGKEY[] = "System\\CurrentControlSet\\Control\\MediaResources\\Joystick\\<FixedKey>\\CurrentJoystickSettings";
static const char TYPENAME[] = "NUKED_SBEMU";
static const char JOYNAME[] = "NUKED SB-EMU Joystick";
static const uint32_t NJ_TYPEFLAGS = 0x00880023;   // HASZ HASPOV ISGAMEPAD HASR HASU
static const uint32_t NJ_TYPEBUTTONS = 12;

// as Windows 98 wrote them for NUKEDJOY.INF (resource I/O 201-201, alternatives 200-20F)
static const uint8_t FORCED[44] = {0x00,0x04,0x00,0x00, 0x00,0x00,0x00,0x00, 0x20,0x00,0x00,0x00, 0x02,0x00,0x00,0x00,
    0x01,0x00,0x02,0x00, 0x01,0x02,0x01,0x02, 0x00,0x00,0x00,0x00, 0xf0,0xff,0x01,0x00, 0x01,0x02,0x01,0x02,
    0x47,0x01,0x00,0x00, 0x00,0x00,0x00,0x00};
static const uint8_t LOGCONF[44] = {0x0a,0x04,0x00,0x00, 0x00,0xe0,0x00,0x00, 0x20,0x00,0x00,0x00, 0x02,0x00,0x00,0x00,
    0x01,0x00,0x0c,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0xff,0xff,0x01,0x00, 0x01,0x02,0x01,0x02,
    0x00,0x00,0x00,0x03, 0x00,0x00,0x00,0x00};

static bool get_str(HKEY root, const std::string& key, const char* name, std::string& out)
{
    HKEY k;
    if (RegOpenKeyExA(root, key.c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    char b[512];
    DWORD sz = sizeof b - 1, t = 0;
    bool ok = RegQueryValueExA(k, name, NULL, &t, (BYTE*)b, &sz) == ERROR_SUCCESS && t == REG_SZ;
    RegCloseKey(k);
    if (ok) { b[sz < sizeof b ? sz : sizeof b - 1] = 0; out = b; }
    return ok;
}

static bool set_val(HKEY root, const std::string& key, const char* name, DWORD type, const void* data, DWORD n)
{
    HKEY k;
    DWORD disp;
    if (RegCreateKeyExA(root, key.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &k, &disp) != ERROR_SUCCESS) return false;
    bool ok = RegSetValueExA(k, name, 0, type, (const BYTE*)data, n) == ERROR_SUCCESS;
    RegCloseKey(k);
    return ok;
}
static bool set_str(HKEY root, const std::string& key, const char* name, const char* v)
{
    return set_val(root, key, name, REG_SZ, v, (DWORD)strlen(v) + 1);
}

static void del_tree(HKEY root, const std::string& key)
{
    HKEY k;
    if (RegOpenKeyExA(root, key.c_str(), 0, KEY_ALL_ACCESS, &k) == ERROR_SUCCESS) {
        char n[256];
        for (;;) {
            DWORD nl = sizeof n;
            if (RegEnumKeyExA(k, 0, n, &nl, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
            del_tree(root, key + "\\" + n);
        }
        RegCloseKey(k);
    }
    RegDeleteKeyA(root, key.c_str());
}

static std::vector<std::string> subkeys(HKEY root, const char* key)
{
    std::vector<std::string> v;
    HKEY k;
    if (RegOpenKeyExA(root, key, 0, KEY_READ, &k) != ERROR_SUCCESS) return v;
    char n[256];
    for (DWORD i = 0;; i++) {
        DWORD nl = sizeof n;
        if (RegEnumKeyExA(k, i, n, &nl, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
        v.push_back(n);
    }
    RegCloseKey(k);
    return v;
}

// Enum\Root\MEDIA\NNNN of our device, "" = none
static std::string device_key()
{
    for (auto& s : subkeys(HKEY_LOCAL_MACHINE, ENUMROOT)) {
        std::string k = std::string(ENUMROOT) + "\\" + s, id;
        if (get_str(HKEY_LOCAL_MACHINE, k, "HardwareID", id) && !_stricmp(id.c_str(), HWID)) return k;
    }
    return "";
}

static std::string sys_dir()
{
    char b[MAX_PATH];
    GetSystemDirectoryA(b, sizeof b);
    return b;
}
static std::string win_dir()
{
    char b[MAX_PATH];
    GetWindowsDirectoryA(b, sizeof b);
    return b;
}

static int nuked_joystick()
{
    for (int n = 1; n <= 16; n++) {
        char v[40];
        snprintf(v, sizeof v, "Joystick%dOEMName", n);
        std::string t;
        if (get_str(HKEY_LOCAL_MACHINE, CFGKEY, v, t) && t == TYPENAME) return n;
    }
    return 0;
}

GamePortState gameport_state()
{
    GamePortState s;
    std::string k = device_key();
    s.device = !k.empty();
    if (s.device) {
        HKEY h;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, k.c_str(), 0, KEY_READ, &h) == ERROR_SUCCESS) {
            uint8_t b[64];
            DWORD sz = sizeof b, t = 0;
            if (RegQueryValueExA(h, "ForcedConfig", NULL, &t, b, &sz) == ERROR_SUCCESS && sz >= 24)
                s.port = b[20] | b[21] << 8;
            RegCloseKey(h);
        }
    }
    std::string n;
    s.type = get_str(HKEY_LOCAL_MACHINE, OEMKEY, "OEMCallout", n);
    s.joystick = nuked_joystick();
    s.vxd = GetFileAttributesA((sys_dir() + "\\NUKEDJOY.VXD").c_str()) != 0xFFFFFFFF;
    return s;
}

static void forced_for(unsigned port, uint8_t* b)
{
    memcpy(b, FORCED, sizeof FORCED);
    b[20] = b[22] = b[32] = b[34] = (uint8_t)port;
    b[21] = b[23] = b[33] = b[35] = (uint8_t)(port >> 8);
}

bool gameport_set_port(unsigned port)
{
    if (port < 0x200 || port > 0x20F) return false;
    std::string k = device_key();
    if (k.empty()) return false;
    GamePortState s = gameport_state();
    if (s.port == port) return false;
    uint8_t b[sizeof FORCED];
    forced_for(port, b);
    return set_val(HKEY_LOCAL_MACHINE, k, "ForcedConfig", REG_BINARY, b, sizeof b);
}

static bool copy_one(const std::string& src, const std::string& dst, std::string& log)
{
    if (CopyFileA(src.c_str(), dst.c_str(), FALSE)) { log += "copied " + dst + "\r\n"; return true; }
    char e[64];
    snprintf(e, sizeof e, " (error %lu)", GetLastError());
    log += "could not copy " + src + " to " + dst + e + "\r\n";
    return false;
}

bool gameport_install(const std::string& dir, unsigned port, std::string& log)
{
    bool restart = false;
    if (port < 0x200 || port > 0x20F) port = 0x201;
    // files: the minidriver where VJoyD looks, the INF where Device Manager finds it again
    std::string sys = sys_dir(), infdir = win_dir() + "\\INF\\OTHER";
    CreateDirectoryA(infdir.c_str(), NULL);
    GamePortState before = gameport_state();
    if (!copy_one(dir + "\\NUKEDJOY.VXD", sys + "\\NUKEDJOY.VXD", log)) return false;
    copy_one(dir + "\\NUKEDJOY.INF", infdir + "\\NUKEDJOY.INF", log);
    if (!before.vxd) restart = true;

    // the joystick type (Game Controllers > Add lists it)
    set_str(HKEY_LOCAL_MACHINE, OEMKEY, "OEMName", JOYNAME);
    set_str(HKEY_LOCAL_MACHINE, OEMKEY, "OEMCallout", "NUKEDJOY.VXD");
    uint32_t hws[2] = {NJ_TYPEFLAGS, NJ_TYPEBUTTONS};
    set_val(HKEY_LOCAL_MACHINE, OEMKEY, "OEMData", REG_BINARY, hws, sizeof hws);
    set_str(HKEY_LOCAL_MACHINE, OEMKEY, "OEMXYLabel", "Left stick");
    set_str(HKEY_LOCAL_MACHINE, OEMKEY, "OEMZLabel", "Triggers");
    set_str(HKEY_LOCAL_MACHINE, OEMKEY, "OEMRLabel", "Right stick up/down");
    set_str(HKEY_LOCAL_MACHINE, OEMKEY, "OEMULabel", "Right stick left/right");
    set_str(HKEY_LOCAL_MACHINE, OEMKEY, "OEMPOVLabel", "D-pad");

    // the device, as the INF install leaves it
    if (!before.device) {
        int cn = 0;                                        // free driver key MEDIA\NNNN
        for (;; cn++) {
            char b[16];
            snprintf(b, sizeof b, "%04d", cn);
            bool used = false;
            for (auto& s : subkeys(HKEY_LOCAL_MACHINE, CLASSKEY)) if (s == b) used = true;
            if (!used) break;
        }
        int en = 0;                                        // free device key Enum\Root\MEDIA\NNNN
        for (;; en++) {
            char b[16];
            snprintf(b, sizeof b, "%04d", en);
            bool used = false;
            for (auto& s : subkeys(HKEY_LOCAL_MACHINE, ENUMROOT)) if (s == b) used = true;
            if (!used) break;
        }
        char cls[64], drv[16], dev[64];
        snprintf(drv, sizeof drv, "MEDIA\\%04d", cn);
        snprintf(cls, sizeof cls, "%s\\%04d", CLASSKEY, cn);
        snprintf(dev, sizeof dev, "%s\\%04d", ENUMROOT, en);
        set_str(HKEY_LOCAL_MACHINE, cls, "DevLoader", "mmdevldr.vxd");
        set_str(HKEY_LOCAL_MACHINE, cls, "Driver", "vjoyd.vxd");
        set_str(HKEY_LOCAL_MACHINE, cls, "InfPath", "NUKEDJOY.INF");
        set_str(HKEY_LOCAL_MACHINE, cls, "InfSection", "NUKGP");
        set_str(HKEY_LOCAL_MACHINE, cls, "ProviderName", "NUKED SB-EMU");
        set_str(HKEY_LOCAL_MACHINE, cls, "DriverDesc", DESC);
        set_str(HKEY_LOCAL_MACHINE, cls, "MatchingDeviceId", HWID);
        std::string d = std::string(cls) + "\\Drivers";
        set_str(HKEY_LOCAL_MACHINE, d, "MIGRATED", "0");
        set_str(HKEY_LOCAL_MACHINE, d, "SubClasses", "joystick");
        set_str(HKEY_LOCAL_MACHINE, d + "\\joystick", "", "");
        set_str(HKEY_LOCAL_MACHINE, d + "\\joystick\\msjstick.drv", "Description", DESC);
        set_str(HKEY_LOCAL_MACHINE, d + "\\joystick\\msjstick.drv", "Driver", "msjstick.drv");

        set_str(HKEY_LOCAL_MACHINE, dev, "Class", "MEDIA");
        set_str(HKEY_LOCAL_MACHINE, dev, "ClassGUID", CLASSGUID);
        set_str(HKEY_LOCAL_MACHINE, dev, "Driver", drv);
        set_str(HKEY_LOCAL_MACHINE, dev, "Mfg", "NUKED SB-EMU");
        set_str(HKEY_LOCAL_MACHINE, dev, "HardwareID", HWID);
        set_str(HKEY_LOCAL_MACHINE, dev, "DeviceDesc", DESC);
        uint32_t flags = 4, caps = 0x14;                   // CONFIGFLAG_MANUAL_INSTALL
        set_val(HKEY_LOCAL_MACHINE, dev, "ConfigFlags", REG_BINARY, &flags, 4);
        set_val(HKEY_LOCAL_MACHINE, dev, "Capabilities", REG_BINARY, &caps, 4);
        uint8_t fc[sizeof FORCED];
        forced_for(port, fc);
        set_val(HKEY_LOCAL_MACHINE, dev, "ForcedConfig", REG_BINARY, fc, sizeof fc);
        // the alternatives Device Manager offers: 201 first, then 200, 202..20F
        for (int i = 0; i < 16; i++) {
            unsigned a = i == 0 ? 0x201 : i == 1 ? 0x200 : 0x200 + i;
            uint8_t lc[sizeof LOGCONF];
            memcpy(lc, LOGCONF, sizeof lc);
            lc[32] = lc[34] = (uint8_t)a;
            lc[33] = lc[35] = (uint8_t)(a >> 8);
            char n[8];
            snprintf(n, sizeof n, "%d", i);
            set_val(HKEY_LOCAL_MACHINE, dev + std::string("\\LogConfig"), n, REG_BINARY, lc, sizeof lc);
        }
        log += std::string("added the NUKED SB-EMU Game Port (") + dev + ", " + drv + ")\r\n";
        restart = true;
    } else if (gameport_set_port(port)) {
        log += "moved the NUKED SB-EMU Game Port\r\n";
        restart = true;
    }

    // a joystick of our type, on the first free joystick number
    if (!before.joystick) {
        int n = 0;
        for (int i = 1; i <= 16 && !n; i++) {
            char v[40];
            std::string t;
            snprintf(v, sizeof v, "Joystick%dOEMName", i);
            if (get_str(HKEY_LOCAL_MACHINE, CFGKEY, v, t) && !t.empty()) continue;
            // free = no type and no hardware settings
            HKEY h;
            uint32_t c[28] = {0};
            DWORD sz = sizeof c, ty;
            snprintf(v, sizeof v, "Joystick%dConfiguration", i);
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, CFGKEY, 0, KEY_READ, &h) == ERROR_SUCCESS) {
                RegQueryValueExA(h, v, NULL, &ty, (BYTE*)c, &sz);
                RegCloseKey(h);
            }
            if (c[0] == 0 && c[1] == 0) n = i;
        }
        if (n) {
            char v[40];
            snprintf(v, sizeof v, "Joystick%dOEMName", n);
            set_str(HKEY_LOCAL_MACHINE, CFGKEY, v, TYPENAME);
            snprintf(v, sizeof v, "Joystick%dOEMCallout", n);
            set_str(HKEY_LOCAL_MACHINE, CFGKEY, v, "NUKEDJOY.VXD");
            uint32_t c[28] = {0};
            c[0] = NJ_TYPEFLAGS; c[1] = NJ_TYPEBUTTONS; c[2] = 6;          // present, OEM type
            for (int i = 0; i < 6; i++) { c[3 + i] = 0; c[9 + i] = 65535; c[15 + i] = 32768; }
            c[21] = 0; c[22] = 18000; c[23] = 27000; c[24] = 9000;
            c[25] = 0x3F;
            c[26] = 13;                                                // JOY_HW_CUSTOM-style OEM type
            snprintf(v, sizeof v, "Joystick%dConfiguration", n);
            set_val(HKEY_LOCAL_MACHINE, CFGKEY, v, REG_BINARY, c, sizeof c);
            char b[80];
            snprintf(b, sizeof b, "Windows joystick %d is now the NUKED SB-EMU Joystick\r\n", n);
            log += b;
            joyConfigChanged(0);
        } else log += "no free joystick number (Game Controllers > Add, NUKED SB-EMU Joystick)\r\n";
    }
    return restart;
}

void gameport_remove(std::string& log)
{
    std::string k = device_key();
    if (!k.empty()) {
        std::string drv;
        if (get_str(HKEY_LOCAL_MACHINE, k, "Driver", drv) && !drv.empty())
            del_tree(HKEY_LOCAL_MACHINE, std::string("System\\CurrentControlSet\\Services\\Class\\") + drv);
        del_tree(HKEY_LOCAL_MACHINE, k);
        log += "removed the NUKED SB-EMU Game Port\r\n";
    }
    int n = nuked_joystick();
    if (n) {
        HKEY h;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, CFGKEY, 0, KEY_ALL_ACCESS, &h) == ERROR_SUCCESS) {
            char v[40];
            snprintf(v, sizeof v, "Joystick%dOEMName", n);
            RegDeleteValueA(h, v);
            snprintf(v, sizeof v, "Joystick%dOEMCallout", n);
            RegDeleteValueA(h, v);
            uint32_t c[28] = {0};
            snprintf(v, sizeof v, "Joystick%dConfiguration", n);
            RegSetValueExA(h, v, 0, REG_BINARY, (BYTE*)c, sizeof c);
            RegCloseKey(h);
        }
        log += "removed the NUKED SB-EMU Joystick\r\n";
        joyConfigChanged(0);
    }
    del_tree(HKEY_LOCAL_MACHINE, OEMKEY);
    // NUKEDJOY.VXD stays until the next start (VJoyD may hold it); delete it then
    std::string vxd = sys_dir() + "\\NUKEDJOY.VXD";
    if (!DeleteFileA(vxd.c_str()))
        WritePrivateProfileStringA("rename", "NUL", vxd.c_str(), (win_dir() + "\\WININIT.INI").c_str());
    DeleteFileA((win_dir() + "\\INF\\OTHER\\NUKEDJOY.INF").c_str());
}
