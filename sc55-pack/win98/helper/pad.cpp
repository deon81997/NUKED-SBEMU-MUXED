/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
// pad.cpp - game pad -> keys / game port for DOS boxes, with a layout per game.
//
// Layouts come from PADMAP.TXT (made by PADCONV from the user's own download of DOSBox Pure's
// keyb2joypad.cpp, the Keyb2Joypad project) and PADUSER.TXT (the user's, same format, wins):
//
//   [Commander Keen 4 - Secret of the Oracle]
//   Id=B408B7B3 ...              FNV-1 of the upper-case 8.3 file name, xor (size << 3)
//   A=LCtrl ; Jump               button = key[+key...]   (None = nothing)
//   LStickX=Left Right           axis = keys for left/up, keys for right/down
//
// Besides keys, a binding can drive the DOS game port: JoyButton1-4, JoyUp/Down/Left/Right,
// Joy2Up/Down/Left/Right (an axis bound to JoyLeft JoyRight etc. passes the analog value).
// Mouse and hat bindings of the Keyb2Joypad data are ignored (the DOS box mouse is Windows').
// [Default] is used when no game matches; PADUSER.TXT may override it too.
#include "pad.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>

// ---------------------------------------------------------------- keys
struct KeyDef { const char* name; uint8_t code; bool ext; };
static const KeyDef KEYS[] = {
    {"1", 0x02, 0}, {"2", 0x03, 0}, {"3", 0x04, 0}, {"4", 0x05, 0}, {"5", 0x06, 0}, {"6", 0x07, 0},
    {"7", 0x08, 0}, {"8", 0x09, 0}, {"9", 0x0A, 0}, {"0", 0x0B, 0},
    {"Q", 0x10, 0}, {"W", 0x11, 0}, {"E", 0x12, 0}, {"R", 0x13, 0}, {"T", 0x14, 0}, {"Y", 0x15, 0},
    {"U", 0x16, 0}, {"I", 0x17, 0}, {"O", 0x18, 0}, {"P", 0x19, 0}, {"A", 0x1E, 0}, {"S", 0x1F, 0},
    {"D", 0x20, 0}, {"F", 0x21, 0}, {"G", 0x22, 0}, {"H", 0x23, 0}, {"J", 0x24, 0}, {"K", 0x25, 0},
    {"L", 0x26, 0}, {"Z", 0x2C, 0}, {"X", 0x2D, 0}, {"C", 0x2E, 0}, {"V", 0x2F, 0}, {"B", 0x30, 0},
    {"N", 0x31, 0}, {"M", 0x32, 0},
    {"F1", 0x3B, 0}, {"F2", 0x3C, 0}, {"F3", 0x3D, 0}, {"F4", 0x3E, 0}, {"F5", 0x3F, 0}, {"F6", 0x40, 0},
    {"F7", 0x41, 0}, {"F8", 0x42, 0}, {"F9", 0x43, 0}, {"F10", 0x44, 0}, {"F11", 0x57, 0}, {"F12", 0x58, 0},
    {"Esc", 0x01, 0}, {"Tab", 0x0F, 0}, {"Backspace", 0x0E, 0}, {"Enter", 0x1C, 0}, {"Space", 0x39, 0},
    {"LAlt", 0x38, 0}, {"RAlt", 0x38, 1}, {"LCtrl", 0x1D, 0}, {"RCtrl", 0x1D, 1},
    {"LShift", 0x2A, 0}, {"RShift", 0x36, 0}, {"CapsLock", 0x3A, 0}, {"ScrollLock", 0x46, 0},
    {"NumLock", 0x45, 0}, {"Grave", 0x29, 0}, {"Minus", 0x0C, 0}, {"Equals", 0x0D, 0},
    {"Backslash", 0x2B, 0}, {"LBracket", 0x1A, 0}, {"RBracket", 0x1B, 0}, {"Semicolon", 0x27, 0},
    {"Quote", 0x28, 0}, {"Period", 0x34, 0}, {"Comma", 0x33, 0}, {"Slash", 0x35, 0},
    {"LessGreater", 0x56, 0}, {"PrintScreen", 0x37, 1}, {"Pause", 0xC5, 0},
    {"Insert", 0x52, 1}, {"Home", 0x47, 1}, {"PageUp", 0x49, 1}, {"Delete", 0x53, 1}, {"End", 0x4F, 1},
    {"PageDown", 0x51, 1}, {"Left", 0x4B, 1}, {"Up", 0x48, 1}, {"Down", 0x50, 1}, {"Right", 0x4D, 1},
    {"KP1", 0x4F, 0}, {"KP2", 0x50, 0}, {"KP3", 0x51, 0}, {"KP4", 0x4B, 0}, {"KP5", 0x4C, 0},
    {"KP6", 0x4D, 0}, {"KP7", 0x47, 0}, {"KP8", 0x48, 0}, {"KP9", 0x49, 0}, {"KP0", 0x52, 0},
    {"KPSlash", 0x35, 1}, {"KPStar", 0x37, 0}, {"KPMinus", 0x4A, 0}, {"KPPlus", 0x4E, 0},
    {"KPEnter", 0x1C, 1}, {"KPPeriod", 0x53, 0},
};
static const int NKEYS = sizeof KEYS / sizeof KEYS[0];

// game port actions, after the keys
enum { A_JB1 = 200, A_JB2, A_JB3, A_JB4, A_JUP, A_JDOWN, A_JLEFT, A_JRIGHT, A_J2UP, A_J2DOWN, A_J2LEFT, A_J2RIGHT };
static const char* const JNAMES[] = {"JoyButton1", "JoyButton2", "JoyButton3", "JoyButton4", "JoyUp", "JoyDown",
                                     "JoyLeft", "JoyRight", "Joy2Up", "Joy2Down", "Joy2Left", "Joy2Right"};

static const char* const BNAMES[PB_COUNT] = {"A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS", "Back",
                                             "Start", "DUp", "DDown", "DLeft", "DRight"};
static const char* const ANAMES[PA_COUNT] = {"LStickX", "LStickY", "RStickX", "RStickY"};

const char* pad_button_name(int b) { return b >= 0 && b < PB_COUNT ? BNAMES[b] : "?"; }

static int action_of(const std::string& n)
{
    for (int i = 0; i < NKEYS; i++) if (!_stricmp(n.c_str(), KEYS[i].name)) return i;
    for (int i = 0; i < 12; i++) if (!_stricmp(n.c_str(), JNAMES[i])) return A_JB1 + i;
    return -1;      // None, mouse, hat, unknown: nothing
}

// ---------------------------------------------------------------- layouts
typedef std::vector<int> Acts;
struct Layout {
    std::string title;
    bool set_b[PB_COUNT] = {}, set_a[PA_COUNT] = {};
    Acts b[PB_COUNT], neg[PA_COUNT], pos[PA_COUNT];
};
static std::vector<Layout> g_lay;                       // [0] = Default
static std::unordered_map<uint32_t, int> g_ids;
static int g_cur = 0;

static std::string upper(std::string s) { for (auto& c : s) c = (char)toupper((unsigned char)c); return s; }
static std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
static Acts parse_keys(const std::string& v)
{
    Acts r;
    size_t p = 0;
    while (p <= v.size()) {
        size_t q = v.find('+', p);
        if (q == std::string::npos) q = v.size();
        int a = action_of(trim(v.substr(p, q - p)));
        if (a >= 0) r.push_back(a);
        p = q + 1;
    }
    return r;
}

static void set_line(Layout& L, const std::string& k, const std::string& v)
{
    for (int i = 0; i < PB_COUNT; i++)
        if (!_stricmp(k.c_str(), BNAMES[i])) { L.b[i] = parse_keys(v); L.set_b[i] = true; return; }
    for (int i = 0; i < PA_COUNT; i++)
        if (!_stricmp(k.c_str(), ANAMES[i])) {
            std::string t = trim(v);
            size_t sp = t.find_first_of(" \t");
            L.neg[i] = parse_keys(sp == std::string::npos ? t : t.substr(0, sp));
            L.pos[i] = sp == std::string::npos ? Acts() : parse_keys(trim(t.substr(sp)));
            L.set_a[i] = true;
            return;
        }
}

static void default_layout(Layout& L)
{
    L = Layout();
    L.title = "Default";
    set_line(L, "LStickX", "JoyLeft JoyRight");
    set_line(L, "LStickY", "JoyUp JoyDown");
    set_line(L, "RStickX", "Joy2Left Joy2Right");
    set_line(L, "RStickY", "Joy2Up Joy2Down");
    set_line(L, "A", "JoyButton1"); set_line(L, "B", "JoyButton2");
    set_line(L, "X", "JoyButton3"); set_line(L, "Y", "JoyButton4");
    set_line(L, "DUp", "Up"); set_line(L, "DDown", "Down"); set_line(L, "DLeft", "Left"); set_line(L, "DRight", "Right");
    set_line(L, "Start", "Enter"); set_line(L, "Back", "Esc");
}

// user = PADUSER.TXT: a section with a known title (or Default) changes only the lines it has
static int load_file(const std::string& path, bool user, std::map<std::string, int>& by_title)
{
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return -1;
    char line[1024];
    int cur = -1, n = 0;
    while (fgets(line, sizeof line, f)) {
        std::string s = line;
        size_t sc = s.find(';');
        if (sc != std::string::npos) s = s.substr(0, sc);
        s = trim(s);
        if (s.empty()) continue;
        if (s[0] == '[') {
            size_t e = s.rfind(']');
            std::string t = trim(s.substr(1, e == std::string::npos ? std::string::npos : e - 1));
            auto it = by_title.find(upper(t));
            if (it != by_title.end()) cur = it->second;
            else {
                Layout L;
                L.title = t;
                g_lay.push_back(L);
                cur = (int)g_lay.size() - 1;
                by_title[upper(t)] = cur;
                n++;
            }
            continue;
        }
        if (cur < 0) continue;
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(s.substr(0, eq)), v = trim(s.substr(eq + 1));
        if (!_stricmp(k.c_str(), "Id")) {
            size_t p = 0;
            while (p < v.size()) {
                size_t q = v.find_first_of(" \t,", p);
                if (q == std::string::npos) q = v.size();
                if (q > p) {
                    uint32_t id = (uint32_t)strtoul(v.substr(p, q - p).c_str(), NULL, 16);
                    if (id) { if (user) g_ids[id] = cur; else g_ids.emplace(id, cur); }
                }
                p = q + 1;
            }
        } else if (_stricmp(k.c_str(), "Year")) set_line(g_lay[cur], k, v);
    }
    fclose(f);
    (void)user;
    return n;
}

int pad_load(const std::string& padmap, const std::string& paduser, std::string& log)
{
    g_lay.clear(); g_ids.clear(); g_cur = 0;
    g_lay.resize(1);
    default_layout(g_lay[0]);
    std::map<std::string, int> by_title;
    by_title["DEFAULT"] = 0;
    int a = load_file(padmap, false, by_title), b = load_file(paduser, true, by_title);
    char m[200];
    snprintf(m, sizeof m, "%s: %s, %s: %s", "PADMAP.TXT", a < 0 ? "none" : (std::to_string(a) + " games").c_str(),
             "PADUSER.TXT", b < 0 ? "none" : (std::to_string(b) + " new").c_str());
    log = m;
    return (a < 0 ? 0 : a) + (b < 0 ? 0 : b);
}

// ---------------------------------------------------------------- game detection
uint32_t pad_file_id(const std::string& name83, uint32_t size)
{
    uint32_t h = 0x811c9dc5u;
    for (unsigned char c : upper(name83)) h = (h * 0x01000193u) ^ c;
    return h ^ (size << 3);
}

static std::string base_name(const std::string& p)
{
    size_t k = p.find_last_of("\\/:");
    return k == std::string::npos ? p : p.substr(k + 1);
}

std::string pad_select(const std::vector<std::pair<std::string, uint32_t>>& progs, std::string& where)
{
    where.clear();
    g_cur = 0;
    // the programs themselves, newest first
    for (size_t i = progs.size(); i-- > 0;) {
        auto it = g_ids.find(pad_file_id(base_name(progs[i].first), progs[i].second));
        if (it != g_ids.end()) { g_cur = it->second; where = progs[i].first; return g_lay[g_cur].title; }
    }
    // the other files in their folders (data files identify a game too)
    for (size_t i = progs.size(); i-- > 0;) {
        std::string dir = progs[i].first.substr(0, progs[i].first.find_last_of('\\') + 1);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((dir + "*.*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        int hit = -1;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::string n = fd.cAlternateFileName[0] ? fd.cAlternateFileName : fd.cFileName;
            auto it = g_ids.find(pad_file_id(n, fd.nFileSizeLow));
            if (it != g_ids.end()) { hit = it->second; where = dir + n; break; }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
        if (hit >= 0) { g_cur = hit; return g_lay[g_cur].title; }
    }
    return g_lay[0].title;
}

// ---------------------------------------------------------------- input -> output
static bool g_down[NKEYS], g_stale[NKEYS];   // down in the DOS box / down there when Windows took the keyboard

static void key_bytes(int k, bool make, std::vector<uint8_t>& o)
{
    const KeyDef& d = KEYS[k];
    if (!strcmp(d.name, "Pause")) {                    // make only, as the real key
        if (make) { static const uint8_t p[] = {0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5}; o.insert(o.end(), p, p + 6); }
        return;
    }
    if (d.ext) o.push_back(0xE0);
    o.push_back((uint8_t)(d.code | (make ? 0 : 0x80)));
}

void pad_release(PadOutput& out)
{
    for (int k = 0; k < NKEYS; k++)
        if (g_down[k]) { key_bytes(k, false, out.scancodes); g_down[k] = false; }
}

void pad_step(const PadInput& in, bool focus_dos, PadOutput& out)
{
    if (g_lay.empty()) { g_lay.resize(1); default_layout(g_lay[0]); }
    const Layout& L = g_lay[g_cur];
    const Layout& D = g_lay[0];
    bool want[NKEYS] = {};
    int jx[4] = {0, 0, 0, 0};          // digital stick pushes, -1/0/1, for A x, A y, B x, B y
    bool analog_set[4] = {};
    uint16_t analog[4] = {32768, 32768, 32768, 32768};
    uint8_t jb = 0;

    auto act = [&](int a) {
        if (a < NKEYS) { want[a] = true; return; }
        switch (a) {
        case A_JB1: jb |= 1; break; case A_JB2: jb |= 2; break; case A_JB3: jb |= 4; break; case A_JB4: jb |= 8; break;
        case A_JUP: jx[1] = -1; break; case A_JDOWN: jx[1] = 1; break;
        case A_JLEFT: jx[0] = -1; break; case A_JRIGHT: jx[0] = 1; break;
        case A_J2UP: jx[3] = -1; break; case A_J2DOWN: jx[3] = 1; break;
        case A_J2LEFT: jx[2] = -1; break; case A_J2RIGHT: jx[2] = 1; break;
        }
    };
    if (in.present) {
        for (int i = 0; i < PB_COUNT; i++) {
            if (!in.button[i]) continue;
            const Acts& a = L.set_b[i] || &L == &D ? L.b[i] : D.b[i];
            for (int x : a) act(x);
        }
        for (int i = 0; i < PA_COUNT; i++) {
            const Layout& S = L.set_a[i] || &L == &D ? L : D;
            const Acts &n = S.neg[i], &p = S.pos[i];
            // an axis bound to a whole game port axis passes the analog value
            int stick = -1;
            if (n.size() == 1 && p.size() == 1) {
                if (n[0] == A_JLEFT && p[0] == A_JRIGHT) stick = 0;
                else if (n[0] == A_JUP && p[0] == A_JDOWN) stick = 1;
                else if (n[0] == A_J2LEFT && p[0] == A_J2RIGHT) stick = 2;
                else if (n[0] == A_J2UP && p[0] == A_J2DOWN) stick = 3;
            }
            if (stick >= 0) {
                analog_set[stick] = true;
                analog[stick] = (uint16_t)(in.axis[i] + 32768);
                continue;
            }
            if (in.axis[i] < -13000) for (int x : n) act(x);
            else if (in.axis[i] > 13000) for (int x : p) act(x);
        }
    }
    // keys: only to a DOS box. When Windows takes the keyboard, held keys are released at
    // once (VKD hands a held Ctrl / Alt / Shift on to Windows; the VxD lets breaks through
    // to Windows) and again when a DOS box has the keyboard back, unless still held there.
    for (int k = 0; k < NKEYS; k++) {
        if (!focus_dos) {
            if (g_down[k]) { key_bytes(k, false, out.scancodes); g_down[k] = false; g_stale[k] = true; }
            continue;
        }
        if (g_stale[k]) {
            g_stale[k] = false;
            if (want[k]) g_down[k] = true;              // still down in the game: nothing to send
            else key_bytes(k, false, out.scancodes);
            continue;
        }
        if (want[k] != g_down[k]) { key_bytes(k, want[k], out.scancodes); g_down[k] = want[k]; }
    }
    // game port: the sticks the layout uses are there, the others are not
    uint8_t used = 0;
    for (int i = 0; i < PB_COUNT; i++)
        for (int x : (L.set_b[i] || &L == &D ? L.b[i] : D.b[i]))
            if (x >= A_JB1) used |= (x >= A_J2UP) ? 2 : 1;
    for (int i = 0; i < PA_COUNT; i++) {
        const Layout& S = L.set_a[i] || &L == &D ? L : D;
        for (int x : S.neg[i]) if (x >= A_JB1) used |= (x >= A_J2UP) ? 2 : 1;
        for (int x : S.pos[i]) if (x >= A_JB1) used |= (x >= A_J2UP) ? 2 : 1;
    }
    for (int s = 0; s < 4; s++) {
        uint16_t v = analog_set[s] ? analog[s] : 32768;
        if (jx[s]) v = jx[s] < 0 ? 0 : 65535;
        out.joy_axis[s] = v;
    }
    out.joy_buttons = jb;
    out.joy_present = in.present ? used : 0;
}
