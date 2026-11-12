/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* padmap.c - game pad buttons -> keyboard keys with a layout per game, for the DOS USB pad (SBEMU) and USBTEST.
 *
 * The same files and rules as the Windows 98 part (win98/helper/pad.cpp), in plain C:
 *
 *   PADMAP.TXT   made by PADCONV from the user's own download of DOSBox Pure's keyb2joypad.cpp (the Keyb2Joypad
 *                project by Jemy Murphy and bigjim; not shipped with the pack):
 *                  [Commander Keen 4 - Secret of the Oracle]
 *                  Id=B408B7B3 ...        FNV-1 (multiply, then xor) of the upper-case 8.3 file name ^ (size << 3)
 *                  A=LCtrl ; Jump         button = key[+key...]   (None = nothing)
 *                  LStickX=Left Right     axis = keys for left/up, keys for right/down
 *   PADUSER.TXT  the user's, same format; a section with a known title (or [Default]) changes only the lines it
 *                has, its Id= lines win over PADMAP's.
 *   PADMAP.BIN   both compiled (PADMAP_Build): sorted id table + one packed entry per layout. The resident SBEMU
 *                loads it whole (a few hundred KB of extended memory) and never reads a file again: layouts are
 *                looked up when a program starts (INT 21h EXEC hook, main.c), and PADMAP_Step runs in the sound
 *                interrupt.
 *
 * Besides keys, a binding can drive the game port: JoyButton1-4, JoyUp/Down/Left/Right, Joy2Up/Down/Left/Right (an
 * axis bound to JoyLeft JoyRight etc. passes the analog value). Mouse and hat bindings are ignored, as on Windows.
 * [Default] (no game found): sticks + A B X Y = game port, D-pad = arrow keys, Start = Enter, Back = Esc.
 *
 * PADMAP.BIN layout (little endian):
 *   0   "PADMAP1" 1Ah
 *   8   6 x u32 stamps: PADMAP.TXT date/time + size, PADUSER.TXT the same, keyb2joypad.cpp the same (0 = missing)
 *   32  u32 layouts, u32 ids, u32 offset of the id table, u32 offset of the layout offsets, u32 offset of the
 *       layout data, u32 total size
 *   ids: (u32 id, u32 layout) sorted by id;  layout offsets: u32 each, from the start of the layout data
 *   a layout: u8 title length, title, u8 slots, then per slot u8 slot (0-15 buttons; 16 + 2 x axis = the axis'
 *   negative side, 17 + 2 x axis its positive side - both written when the axis is set), u8 count, the actions
 *   (u8: key index below, or 200 + game port action). Layout 0 = Default with every slot written. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "padmap.h"
#ifdef __DJGPP__
#include <dos.h>
#include <fcntl.h>
#include <unistd.h>
#else
#include <sys/stat.h>
#include <dirent.h>
#endif

/* ---------------------------------------------------------------- keys (same names and codes as pad.cpp) */
typedef struct { const char* name; uint8_t code; uint8_t ext; } KeyDef;
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
#define NKEYS (int)(sizeof KEYS / sizeof KEYS[0])

enum { A_JB1 = 200, A_JB2, A_JB3, A_JB4, A_JUP, A_JDOWN, A_JLEFT, A_JRIGHT, A_J2UP, A_J2DOWN, A_J2LEFT, A_J2RIGHT, A_END };
static const char* const JNAMES[] = { "JoyButton1", "JoyButton2", "JoyButton3", "JoyButton4", "JoyUp", "JoyDown",
                                      "JoyLeft", "JoyRight", "Joy2Up", "Joy2Down", "Joy2Left", "Joy2Right" };
static const char* const BNAMES[PM_BUTTONS] = { "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "LS", "RS", "Back",
                                                "Start", "DUp", "DDown", "DLeft", "DRight" };
static const char* const ANAMES[PM_AXES] = { "LStickX", "LStickY", "RStickX", "RStickY" };

#define NSLOT (PM_BUTTONS + 2 * PM_AXES)    /* 24 */
#define MAXA  6                             /* actions per slot */

const char* PADMAP_ButtonName(int b) { return b >= 0 && b < PM_BUTTONS ? BNAMES[b] : "?"; }

static int ieq(const char* a, const char* b)
{
    for (; *a && toupper((unsigned char)*a) == toupper((unsigned char)*b); ++a, ++b) {}
    return toupper((unsigned char)*a) == toupper((unsigned char)*b);
}
static int ieqn(const char* a, int n, const char* b)    /* a[0..n) == b, no case */
{
    int i;
    for (i = 0; i < n; ++i) if (!b[i] || toupper((unsigned char)a[i]) != toupper((unsigned char)b[i])) return 0;
    return b[n] == 0;
}

static int action_of(const char* s, int n)
{
    int i;
    for (i = 0; i < NKEYS; i++) if (ieqn(s, n, KEYS[i].name)) return i;
    for (i = 0; i < 12; i++) if (ieqn(s, n, JNAMES[i])) return A_JB1 + i;
    return -1;      /* None, mouse, hat, unknown: nothing */
}
static const char* action_name(int a) { return a < NKEYS ? KEYS[a].name : a >= A_JB1 && a < A_END ? JNAMES[a - A_JB1] : "?"; }

uint32_t PADMAP_FileId(const char* name83, uint32_t size)
{
    uint32_t h = 0x811c9dc5u;
    for (; *name83; ++name83) h = (h * 0x01000193u) ^ (uint8_t)toupper((unsigned char)*name83);
    return h ^ (size << 3);
}

/* ================================================================ building PADMAP.BIN */
typedef struct
{
    char*    title;
    uint32_t set;                      /* bit per slot */
    uint8_t  n[NSLOT];
    uint8_t  a[NSLOT][MAXA];
} BLay;

static BLay*     B_lay;
static int       B_n, B_cap;
static int*      B_th;                 /* title hash -> layout + 1 */
static uint32_t* B_ik; static int* B_iv; /* id hash: key, layout + 1 */
#define TH_SIZE 16384
#define IH_SIZE 32768

static uint32_t thash(const char* s)
{
    uint32_t h = 0x811c9dc5u;
    for (; *s; ++s) h = (h ^ (uint8_t)toupper((unsigned char)*s)) * 0x01000193u;
    return h;
}
static int title_find(const char* t)
{
    uint32_t i = thash(t) & (TH_SIZE - 1);
    while (B_th[i]) { if (ieq(B_lay[B_th[i] - 1].title, t)) return B_th[i] - 1; i = (i + 1) & (TH_SIZE - 1); }
    return -1;
}
static int lay_new(const char* t)
{
    uint32_t i;
    BLay* L;
    if (B_n >= TH_SIZE / 2) return -1;
    if (B_n == B_cap)
    {
        int nc = B_cap ? B_cap * 2 : 1024;
        BLay* nl = (BLay*)realloc(B_lay, nc * sizeof(BLay));
        if (!nl) return -1;
        B_lay = nl; B_cap = nc;
    }
    L = &B_lay[B_n];
    memset(L, 0, sizeof *L);
    L->title = (char*)malloc(strlen(t) + 1);
    if (!L->title) return -1;
    strcpy(L->title, t);
    for (i = thash(t) & (TH_SIZE - 1); B_th[i]; i = (i + 1) & (TH_SIZE - 1)) {}
    B_th[i] = B_n + 1;
    return B_n++;
}
static int ids_n;
static void id_put(uint32_t id, int lay, int over)
{
    uint32_t i = (id * 2654435761u) & (IH_SIZE - 1);
    while (B_iv[i]) { if (B_ik[i] == id) { if (over) B_iv[i] = lay + 1; return; } i = (i + 1) & (IH_SIZE - 1); }
    if (ids_n >= IH_SIZE / 2) return;
    B_ik[i] = id; B_iv[i] = lay + 1; ++ids_n;
}

static const char* skipsp(const char* s) { while (*s == ' ' || *s == '\t') ++s; return s; }

/* "LCtrl+Space" -> actions */
static void parse_keys(const char* s, int len, uint8_t* a, uint8_t* n)
{
    int p = 0;
    *n = 0;
    while (p < len)
    {
        int q = p, e;
        while (q < len && s[q] != '+') ++q;
        e = q;
        while (p < e && (s[p] == ' ' || s[p] == '\t')) ++p;
        while (e > p && (s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
        if (e > p)
        {
            int x = action_of(s + p, e - p);
            if (x >= 0 && *n < MAXA) a[(*n)++] = (uint8_t)x;
        }
        p = q + 1;
    }
}

static void set_line(BLay* L, const char* k, int klen, const char* v)
{
    int i, vl = (int)strlen(v);
    for (i = 0; i < PM_BUTTONS; i++)
        if (ieqn(k, klen, BNAMES[i])) { parse_keys(v, vl, L->a[i], &L->n[i]); L->set |= 1u << i; return; }
    for (i = 0; i < PM_AXES; i++)
        if (ieqn(k, klen, ANAMES[i]))
        {
            int sp = 0, s0 = PM_BUTTONS + 2 * i;
            while (sp < vl && v[sp] != ' ' && v[sp] != '\t') ++sp;
            parse_keys(v, sp, L->a[s0], &L->n[s0]);
            { const char* r = skipsp(v + sp); parse_keys(r, (int)strlen(r), L->a[s0 + 1], &L->n[s0 + 1]); }
            L->set |= 3u << s0;
            return;
        }
}

static void default_layout(BLay* L)
{
    static const char* const d[][2] = {
        { "LStickX", "JoyLeft JoyRight" }, { "LStickY", "JoyUp JoyDown" }, { "RStickX", "Joy2Left Joy2Right" },
        { "RStickY", "Joy2Up Joy2Down" }, { "A", "JoyButton1" }, { "B", "JoyButton2" }, { "X", "JoyButton3" },
        { "Y", "JoyButton4" }, { "DUp", "Up" }, { "DDown", "Down" }, { "DLeft", "Left" }, { "DRight", "Right" },
        { "Start", "Enter" }, { "Back", "Esc" } };
    int i;
    for (i = 0; i < (int)(sizeof d / sizeof d[0]); ++i) set_line(L, d[i][0], (int)strlen(d[i][0]), d[i][1]);
    L->set = (1u << NSLOT) - 1;        /* every slot: the unset ones are "nothing" */
}

static int load_text(const char* path, int user)
{
    FILE* f = fopen(path, "r");
    char line[4096];
    int cur = -1, n = 0;
    if (!f) return -1;
    while (fgets(line, sizeof line, f))
    {
        char* s = line;
        char* e;
        char* sc = strchr(s, ';');
        if (sc) *sc = 0;
        s = (char*)skipsp(s);
        e = s + strlen(s);
        while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
        if (!*s) continue;
        if (*s == '[')
        {
            char* t = s + 1;
            char* r = strrchr(t, ']');
            if (r) *r = 0;
            t = (char*)skipsp(t);
            e = t + strlen(t);
            while (e > t && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
            cur = title_find(t);
            if (cur < 0) { cur = lay_new(t); if (cur >= 0) ++n; }
            continue;
        }
        if (cur < 0) continue;
        {
            char* eq = strchr(s, '=');
            char* k = s;
            char* v;
            int kl;
            if (!eq) continue;
            v = (char*)skipsp(eq + 1);
            kl = (int)(eq - s);
            while (kl > 0 && (k[kl - 1] == ' ' || k[kl - 1] == '\t')) --kl;
            if (ieqn(k, kl, "Id"))
            {
                while (*v)
                {
                    char* q;
                    uint32_t id = (uint32_t)strtoul(v, &q, 16);
                    if (q == v) { ++v; continue; }
                    if (id) id_put(id, cur, user);
                    v = q;
                    while (*v == ' ' || *v == '\t' || *v == ',') ++v;
                }
            }
            else if (!ieqn(k, kl, "Year")) set_line(&B_lay[cur], k, kl, v);
        }
    }
    fclose(f);
    return n;
}

static void build_free(void)
{
    int i;
    for (i = 0; i < B_n; ++i) free(B_lay[i].title);
    free(B_lay); free(B_th); free(B_ik); free(B_iv);
    B_lay = NULL; B_th = NULL; B_ik = NULL; B_iv = NULL; B_n = B_cap = 0; ids_n = 0;
}

typedef struct { uint32_t id, lay; } IdEnt;
static int idcmp(const void* a, const void* b)
{
    uint32_t x = ((const IdEnt*)a)->id, y = ((const IdEnt*)b)->id;
    return x < y ? -1 : x > y;
}

static void put32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static uint32_t get32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

int PADMAP_Build(const char* txt, const char* user, const char* bin, const uint32_t stamps[6], char* msg, int msglen)
{
    int a, b, i, j, k;
    size_t dsize = 0, total, off_ids, off_lay, off_data, pos;
    uint8_t* out;
    IdEnt* ids;
    FILE* f;
    build_free();
    B_th = (int*)calloc(TH_SIZE, sizeof(int));
    B_ik = (uint32_t*)calloc(IH_SIZE, sizeof(uint32_t));
    B_iv = (int*)calloc(IH_SIZE, sizeof(int));
    if (!B_th || !B_ik || !B_iv || lay_new("Default") != 0) { build_free(); snprintf(msg, msglen, "out of memory"); return 1; }
    default_layout(&B_lay[0]);
    a = txt ? load_text(txt, 0) : -1;
    b = user ? load_text(user, 1) : -1;
    if (a < 0 && b < 0) { build_free(); snprintf(msg, msglen, "no PADMAP.TXT or PADUSER.TXT"); return 1; }
    /* sizes */
    for (i = 0; i < B_n; ++i)
    {
        size_t tl = strlen(B_lay[i].title);
        dsize += 2 + (tl > 255 ? 255 : tl);
        for (j = 0; j < NSLOT; ++j) if (B_lay[i].set & (1u << j)) dsize += 2 + B_lay[i].n[j];
    }
    off_ids = 56;
    off_lay = off_ids + (size_t)ids_n * 8;
    off_data = off_lay + (size_t)B_n * 4;
    total = off_data + dsize;
    out = (uint8_t*)calloc(1, total);
    ids = (IdEnt*)malloc((ids_n ? ids_n : 1) * sizeof(IdEnt));
    if (!out || !ids) { free(out); free(ids); build_free(); snprintf(msg, msglen, "out of memory"); return 1; }
    memcpy(out, "PADMAP1\x1A", 8);
    for (i = 0; i < 6; ++i) put32(out + 8 + i * 4, stamps ? stamps[i] : 0);
    put32(out + 32, (uint32_t)B_n); put32(out + 36, (uint32_t)ids_n);
    put32(out + 40, (uint32_t)off_ids); put32(out + 44, (uint32_t)off_lay); put32(out + 48, (uint32_t)off_data);
    put32(out + 52, (uint32_t)total);
    for (i = 0, k = 0; i < IH_SIZE; ++i) if (B_iv[i]) { ids[k].id = B_ik[i]; ids[k].lay = (uint32_t)(B_iv[i] - 1); ++k; }
    qsort(ids, ids_n, sizeof(IdEnt), idcmp);
    for (i = 0; i < ids_n; ++i) { put32(out + off_ids + i * 8, ids[i].id); put32(out + off_ids + i * 8 + 4, ids[i].lay); }
    pos = 0;
    for (i = 0; i < B_n; ++i)
    {
        uint8_t* p = out + off_data;
        size_t tl = strlen(B_lay[i].title);
        int ns = 0;
        if (tl > 255) tl = 255;
        put32(out + off_lay + i * 4, (uint32_t)pos);
        p[pos++] = (uint8_t)tl;
        memcpy(p + pos, B_lay[i].title, tl); pos += tl;
        for (j = 0; j < NSLOT; ++j) if (B_lay[i].set & (1u << j)) ++ns;
        p[pos++] = (uint8_t)ns;
        for (j = 0; j < NSLOT; ++j)
            if (B_lay[i].set & (1u << j))
            {
                p[pos++] = (uint8_t)j;
                p[pos++] = B_lay[i].n[j];
                memcpy(p + pos, B_lay[i].a[j], B_lay[i].n[j]); pos += B_lay[i].n[j];
            }
    }
    f = fopen(bin, "wb");
    k = f && fwrite(out, 1, total, f) == total;
    if (f && fclose(f)) k = 0;
    if (!k) remove(bin);
    if (k) snprintf(msg, msglen, "%d games, %d file ids (PADMAP.TXT %s, PADUSER.TXT %s)",
                    B_n - 1, ids_n, a < 0 ? "none" : "used", b < 0 ? "none" : "used");
    else snprintf(msg, msglen, "could not write %s", bin);
    free(out); free(ids);
    build_free();
    return k ? 0 : 1;
}

/* ================================================================ the loaded table */
static uint8_t*        M_bin;          /* whole PADMAP.BIN, or NULL = built-in Default only */
static uint32_t        M_nlay, M_nids;
static const uint8_t*  M_ids;
static const uint8_t*  M_layoff;
static const uint8_t*  M_data;
static uint8_t         M_def[256];     /* built-in Default when there is no file */

static void builtin_default(void)
{
    BLay L;
    int j, pos = 0;
    memset(&L, 0, sizeof L);
    default_layout(&L);
    M_def[pos++] = 7; memcpy(M_def + pos, "Default", 7); pos += 7;
    M_def[pos++] = NSLOT;
    for (j = 0; j < NSLOT; ++j) { M_def[pos++] = (uint8_t)j; M_def[pos++] = L.n[j]; memcpy(M_def + pos, L.a[j], L.n[j]); pos += L.n[j]; }
}

static const uint8_t* lay_ptr(int lay)
{
    if (!M_def[0]) builtin_default();
    if (!M_bin || lay < 0 || (uint32_t)lay >= M_nlay) return lay == 0 || !M_bin ? M_def : NULL;
    return M_data + get32(M_layoff + lay * 4);
}

void PADMAP_Reset(void)
{
    free(M_bin);
    M_bin = NULL; M_nlay = 1; M_nids = 0;
    builtin_default();
    PADMAP_Select(0);
}

void PADMAP_Stamp(const char* path, uint32_t* stamp, uint32_t* size)
{
    *stamp = 0; *size = 0;
#ifdef __DJGPP__
    {
        int h;
        unsigned int date = 0, time = 0;
        if (_dos_open(path, O_RDONLY, &h) == 0)
        {
            long sz = 0;
            if (_dos_getftime(h, &date, &time) == 0) *stamp = ((uint32_t)date << 16) | time;
            sz = lseek(h, 0, SEEK_END);
            *size = sz > 0 ? (uint32_t)sz : 0;
            _dos_close(h);
            if (!*stamp) *stamp = 1;
        }
    }
#else
    {
        struct stat st;
        if (stat(path, &st) == 0) { *stamp = (uint32_t)st.st_mtime | 1; *size = (uint32_t)st.st_size; }
    }
#endif
}

int PADMAP_Load(const char* bin, uint32_t stamps[6], char* msg, int msglen)
{
    FILE* f = fopen(bin, "rb");
    long sz;
    uint8_t* p;
    int i;
    PADMAP_Reset();
    if (!f) { snprintf(msg, msglen, "no %s", bin); return 1; }
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    p = sz >= 56 && sz < 16L * 1024 * 1024 ? (uint8_t*)malloc(sz) : NULL;
    if (!p || fread(p, 1, sz, f) != (size_t)sz) { fclose(f); free(p); snprintf(msg, msglen, "%s unreadable", bin); return 1; }
    fclose(f);
    if (memcmp(p, "PADMAP1\x1A", 8) || get32(p + 52) != (uint32_t)sz || get32(p + 32) == 0
        || get32(p + 40) + (uint64_t)get32(p + 36) * 8 > (uint64_t)sz || get32(p + 44) + (uint64_t)get32(p + 32) * 4 > (uint64_t)sz
        || get32(p + 48) > (uint32_t)sz)
    { free(p); snprintf(msg, msglen, "%s is not a layout file", bin); return 1; }
    if (stamps) for (i = 0; i < 6; ++i) stamps[i] = get32(p + 8 + i * 4);
    M_bin = p;
    M_nlay = get32(p + 32); M_nids = get32(p + 36);
    M_ids = p + get32(p + 40); M_layoff = p + get32(p + 44); M_data = p + get32(p + 48);
    for (i = 0; i < (int)M_nlay; ++i)                    /* every entry must lie inside the file */
    {
        uint32_t o = get32(M_layoff + i * 4), e;
        const uint8_t* L;
        int s, ns;
        if (M_data + o + 2 > p + sz) break;
        L = M_data + o;
        e = 1 + L[0];
        if (M_data + o + e + 1 > p + sz) break;
        ns = L[e++];
        for (s = 0; s < ns && M_data + o + e + 2 <= p + sz; ++s) e += 2 + L[e + 1];
        if (s < ns || M_data + o + e > p + sz) break;
    }
    if (i < (int)M_nlay) { PADMAP_Reset(); snprintf(msg, msglen, "%s is damaged", bin); return 1; }
    PADMAP_Select(0);
    snprintf(msg, msglen, "%u games, %u file ids", (unsigned)M_nlay - 1, (unsigned)M_nids);
    return 0;
}

int PADMAP_Find(uint32_t id)
{
    uint32_t lo = 0, hi = M_nids;
    if (!M_bin) return -1;
    while (lo < hi)
    {
        uint32_t mid = (lo + hi) / 2, v = get32(M_ids + mid * 8);
        if (v == id) return (int)get32(M_ids + mid * 8 + 4);
        if (v < id) lo = mid + 1; else hi = mid;
    }
    return -1;
}

int PADMAP_Count(int* ids) { if (ids) *ids = (int)M_nids; return M_bin ? (int)M_nlay : 1; }

const char* PADMAP_Title(int lay)
{
    static char t[2][256];
    static int w;
    const uint8_t* L = lay_ptr(lay);
    if (!L) return "?";
    w ^= 1;
    memcpy(t[w], L + 1, L[0]); t[w][L[0]] = 0;
    return t[w];
}

/* ================================================================ resolved layout + step */
typedef struct { uint8_t n[NSLOT]; uint8_t a[NSLOT][MAXA]; } RLay;

static void decode(int lay, RLay* R)
{
    const uint8_t* D = lay_ptr(0);
    const uint8_t* L = lay_ptr(lay);
    int pass, s;
    memset(R, 0, sizeof *R);
    for (pass = 0; pass < 2; ++pass)                     /* Default first, then the game's own lines over it */
    {
        const uint8_t* p = pass ? L : D;
        int ns, e;
        if (!p || (pass && p == D)) continue;
        e = 1 + p[0];
        ns = p[e++];
        for (s = 0; s < ns; ++s)
        {
            int slot = p[e], n = p[e + 1];
            if (slot < NSLOT)
            {
                R->n[slot] = (uint8_t)(n > MAXA ? MAXA : n);
                memcpy(R->a[slot], p + e + 2, R->n[slot]);
            }
            e += 2 + n;
        }
    }
}

static volatile int S_pend;
static int      S_thr = 13000;          /* stick -> keys: past this (PADMAP_SetDeadZone) */
static int      S_cur = -1;
static RLay     S_r;
static uint16_t S_taken; static uint8_t S_axtaken, S_used;
static uint8_t  S_down[NKEYS];

void PADMAP_Select(int lay) { S_pend = lay < 0 ? 0 : lay; }

/* 10% (the default /JOYDZ) = 13000 as SC55W; each percent more or less moves it by about 1% of the travel */
void PADMAP_SetDeadZone(int pct)
{
    int t = 13000 + (pct - 10) * 328;
    S_thr = t < 3300 ? 3300 : t > 31000 ? 31000 : t;
}
int  PADMAP_Current(void) { return S_pend; }

static void key_bytes(int k, int make, PADMAP_EmitFn emit)
{
    uint8_t b[6];
    int n = 0;
    if (!strcmp(KEYS[k].name, "Pause"))
    {
        static const uint8_t p[6] = { 0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5 };   /* make only, as the real key */
        if (make) emit(p, 6);
        return;
    }
    if (KEYS[k].ext) b[n++] = 0xE0;
    b[n++] = (uint8_t)(KEYS[k].code | (make ? 0 : 0x80));
    emit(b, n);
}

void PADMAP_Release(PADMAP_EmitFn emit)
{
    int k;
    for (k = 0; k < NKEYS; k++) if (S_down[k]) { S_down[k] = 0; if (emit) key_bytes(k, 0, emit); }
}

static void apply(void)
{
    int i, j;
    S_cur = S_pend;
    decode(S_cur, &S_r);
    S_taken = 0; S_axtaken = 0; S_used = 0;
    for (i = 0; i < NSLOT; ++i)
    {
        int keys = 0;
        for (j = 0; j < S_r.n[i]; ++j)
        {
            int x = S_r.a[i][j];
            if (x < NKEYS) keys = 1;
            else if (x >= A_JB1) S_used |= (x >= A_J2UP) ? 2 : 1;
        }
        if (!keys) continue;
        if (i < PM_BUTTONS) S_taken |= (uint16_t)(1u << i);
        else S_axtaken |= (uint8_t)(1u << ((i - PM_BUTTONS) / 2));
    }
    for (i = 0; i < PM_BUTTONS; ++i)      /* a button bound to keys and the game port is not "keys only" */
        for (j = 0; j < S_r.n[i]; ++j) if (S_r.a[i][j] >= A_JB1) S_taken &= (uint16_t)~(1u << i);
}

void PADMAP_Step(const PADMAP_In* in, int npads, PADMAP_Out* out, PADMAP_EmitFn emit)
{
    uint8_t want[NKEYS];
    int jx[4] = { 0, 0, 0, 0 }, aset[4] = { 0, 0, 0, 0 }, analog[4] = { 0, 0, 0, 0 };
    uint8_t jb = 0;
    int k, i, j;
    if (S_cur != S_pend) { PADMAP_Release(emit); apply(); }
    memset(want, 0, sizeof want);
    for (k = 0; k < npads; ++k)
    {
        const PADMAP_In* p = &in[k];
        if (!p->present) continue;
        for (i = 0; i < NSLOT; ++i)
        {
            int on;
            if (i < PM_BUTTONS) on = (p->buttons >> i) & 1;
            else
            {
                int ax = (i - PM_BUTTONS) / 2, neg = !((i - PM_BUTTONS) & 1), v = p->axis[ax];
                /* an axis bound to a whole game port axis passes the analog value (pad 1) */
                if (neg && S_r.n[i] == 1 && S_r.n[i + 1] == 1)
                {
                    int a = S_r.a[i][0], b = S_r.a[i + 1][0], st = -1;
                    if (a == A_JLEFT && b == A_JRIGHT) st = 0;
                    else if (a == A_JUP && b == A_JDOWN) st = 1;
                    else if (a == A_J2LEFT && b == A_J2RIGHT) st = 2;
                    else if (a == A_J2UP && b == A_J2DOWN) st = 3;
                    if (st >= 0) { if (k == 0) { aset[st] = 1; analog[st] = v; } ++i; continue; }
                }
                on = neg ? v < -S_thr : v > S_thr;
            }
            if (!on) continue;
            for (j = 0; j < S_r.n[i]; ++j)
            {
                int x = S_r.a[i][j];
                if (x < NKEYS) { want[x] = 1; continue; }
                if (k) continue;                         /* the game port follows pad 1 */
                switch (x)
                {
                case A_JB1: jb |= 1; break; case A_JB2: jb |= 2; break; case A_JB3: jb |= 4; break; case A_JB4: jb |= 8; break;
                case A_JUP: jx[1] = -1; break; case A_JDOWN: jx[1] = 1; break;
                case A_JLEFT: jx[0] = -1; break; case A_JRIGHT: jx[0] = 1; break;
                case A_J2UP: jx[3] = -1; break; case A_J2DOWN: jx[3] = 1; break;
                case A_J2LEFT: jx[2] = -1; break; case A_J2RIGHT: jx[2] = 1; break;
                }
            }
        }
    }
    for (k = 0; k < NKEYS; k++)
        if (want[k] != S_down[k]) { S_down[k] = want[k]; if (emit) key_bytes(k, want[k], emit); }
    if (!out) return;
    out->game = S_cur != 0;
    for (i = 0; i < 4; ++i) out->axis[i] = jx[i] ? (jx[i] < 0 ? -32768 : 32767) : aset[i] ? analog[i] : 0;
    out->buttons = jb;
    out->present = npads > 0 && in[0].present ? S_used : 0;
    out->taken = S_taken;
    out->axis_taken = S_axtaken;
}

/* "A=LCtrl B=Space ... LStickX=Left/Right": what the layout does (Default's lines where it has none) */
int PADMAP_Describe(int lay, char* out, int size)
{
    RLay R;
    int i, j, len = 0;
    if (size <= 0) return 0;
    out[0] = 0;
    decode(lay, &R);
    for (i = 0; i < NSLOT; ++i)
    {
        char item[96];
        int il = 0;
        int ax = i >= PM_BUTTONS, s2 = ax ? i + 1 : i;
        if (ax && ((i - PM_BUTTONS) & 1)) continue;
        if (!R.n[i] && !R.n[s2]) continue;
        il = snprintf(item, sizeof item, "%s%s=", len ? " " : "", ax ? ANAMES[(i - PM_BUTTONS) / 2] : BNAMES[i]);
        for (j = 0; j < R.n[i] && il < (int)sizeof item - 1; ++j)
            il += snprintf(item + il, sizeof item - il, "%s%s", j ? "+" : "", action_name(R.a[i][j]));
        if (ax)
        {
            il += snprintf(item + il, sizeof item - il, "/");
            for (j = 0; j < R.n[s2] && il < (int)sizeof item - 1; ++j)
                il += snprintf(item + il, sizeof item - il, "%s%s", j ? "+" : "", action_name(R.a[s2][j]));
        }
        if (len + (int)strlen(item) >= size - 1) break;
        strcpy(out + len, item);
        len += (int)strlen(item);
    }
    return len;
}

/* ================================================================ start-up (SBEMU, USBTEST) */
extern int padconv_run(const char* in, const char* out, char* msg, int msglen);

/* first file matching dir\pattern-suffix (".CPP"), 8.3 name into name; 0 = none */
static int first_with(const char* dir, const char* ext, char* name, int len)
{
#ifdef __DJGPP__
    char pat[300];
    struct find_t f;
    snprintf(pat, sizeof pat, "%s\\*%s", dir, ext);
    if (_dos_findfirst(pat, _A_NORMAL | _A_RDONLY | _A_ARCH, &f)) return 0;
    snprintf(name, len, "%s", f.name);
    return 1;
#else
    DIR* d = opendir(dir);
    struct dirent* e;
    int el = (int)strlen(ext), ok = 0;
    if (!d) return 0;
    while (!ok && (e = readdir(d)) != NULL)
    {
        int nl = (int)strlen(e->d_name);
        if (nl > el && ieq(e->d_name + nl - el, ext)) { snprintf(name, len, "%s", e->d_name); ok = 1; }
    }
    closedir(d);
    return ok;
#endif
}

int PADMAP_Prepare(const char* dir, PADMAP_NoteFn note)
{
    char txt[300], user[300], bin[300], pdir[300], cpp[400], nm[64], msg[200];
    uint32_t now[6], was[6];
    int i, rebuild = 0, ids = 0, n;
    snprintf(txt, sizeof txt, "%s\\PADMAP.TXT", dir);
    snprintf(user, sizeof user, "%s\\PADUSER.TXT", dir);
    snprintf(bin, sizeof bin, "%s\\PADMAP.BIN", dir);
    snprintf(pdir, sizeof pdir, "%s\\PADMAP", dir);
#ifndef __DJGPP__
    for (i = 0; txt[i]; ++i) if (txt[i] == '\\') txt[i] = '/';
    for (i = 0; user[i]; ++i) if (user[i] == '\\') user[i] = '/';
    for (i = 0; bin[i]; ++i) if (bin[i] == '\\') bin[i] = '/';
    for (i = 0; pdir[i]; ++i) if (pdir[i] == '\\') pdir[i] = '/';
#endif
    cpp[0] = 0;
    if (first_with(pdir, ".CPP", nm, sizeof nm))        /* keyb2joypad.cpp (KEYB2J~1.CPP in plain DOS) */
        snprintf(cpp, sizeof cpp, "%s\\%s", pdir, nm);
#ifndef __DJGPP__
    for (i = 0; cpp[i]; ++i) if (cpp[i] == '\\') cpp[i] = '/';
#endif
    PADMAP_Stamp(txt, &now[0], &now[1]);
    PADMAP_Stamp(user, &now[2], &now[3]);
    if (cpp[0]) PADMAP_Stamp(cpp, &now[4], &now[5]); else now[4] = now[5] = 0;
    memset(was, 0, sizeof was);
    if (PADMAP_Load(bin, was, msg, sizeof msg) != 0) rebuild = 1;
    if (cpp[0] && (!now[0] || was[4] != now[4] || was[5] != now[5]))
    {
        char m2[360];
        snprintf(m2, sizeof m2, "making PADMAP.TXT from %s", cpp);
        if (note) note(0, m2);
        if (padconv_run(cpp, txt, msg, sizeof msg) == 0)
        { snprintf(m2, sizeof m2, "PADMAP.TXT made from %s: %s", cpp, msg); if (note) note(2, m2); rebuild = 1; }
        else { snprintf(m2, sizeof m2, "%s not converted: %s", cpp, msg); if (note) note(1, m2); }
        PADMAP_Stamp(txt, &now[0], &now[1]);
    }
    if (!now[0] && !now[2])
    {
        char m2[360];
        PADMAP_Reset();
        snprintf(m2, sizeof m2, "no PADMAP.TXT or PADUSER.TXT in %s (keyb2joypad.cpp: %s)", dir, cpp[0] ? cpp : "none");
        if (note) note(2, m2);
        return 0;
    }
    for (i = 0; i < 4; ++i) if (was[i] != now[i]) rebuild = 1;
    if (rebuild)
    {
        if (note) note(0, "making PADMAP.BIN");
        if (PADMAP_Build(now[0] ? txt : NULL, now[2] ? user : NULL, bin, now, msg, sizeof msg) != 0
            || PADMAP_Load(bin, NULL, msg, sizeof msg) != 0)
        {
            if (note) note(1, msg);
            PADMAP_Reset();
            return 0;
        }
        if (note) note(2, "PADMAP.BIN rebuilt");
    }
    n = PADMAP_Count(&ids);
    {
        char m2[360];
        snprintf(m2, sizeof m2, "%d games, %d file ids (PADMAP.TXT %s, PADUSER.TXT %s, keyb2joypad.cpp %s)", n - 1, ids,
                 now[0] ? "yes" : "no", now[2] ? "yes" : "no", cpp[0] ? cpp : "no");
        if (note) note(2, m2);
    }
    return 1;
}

/* name and size of one file (8.3 name in plain DOS); 0 = not found */
static int file_info(const char* path, char* name, int len, uint32_t* size)
{
#ifdef __DJGPP__
    struct find_t f;
    if (_dos_findfirst(path, _A_NORMAL | _A_RDONLY | _A_ARCH | _A_HIDDEN | _A_SYSTEM, &f)) return 0;
    snprintf(name, len, "%s", f.name);
    *size = f.size;
    return 1;
#else
    struct stat st;
    const char* b = strrchr(path, '/');
    if (stat(path, &st) != 0) return 0;
    snprintf(name, len, "%s", b ? b + 1 : path);
    *size = (uint32_t)st.st_size;
    return 1;
#endif
}

int PADMAP_ForProgram(const char* path, char* where, int wherelen, int* how)
{
    char name[64], dir[300];
    uint32_t size;
    int lay, i;
    char* sl;
    *how = 0;
    if (where && wherelen) where[0] = 0;
    if (file_info(path, name, sizeof name, &size) && (lay = PADMAP_Find(PADMAP_FileId(name, size))) >= 0)
    {
        if (where) snprintf(where, wherelen, "%s", name);
        *how = 2;
        return lay;
    }
    snprintf(dir, sizeof dir, "%s", path);
    sl = strrchr(dir, '\\');
    if (!sl) sl = strrchr(dir, '/');
    if (!sl) sl = strrchr(dir, ':');
    if (sl) sl[1] = 0; else dir[0] = 0;
#ifdef __DJGPP__
    {
        struct find_t f;
        char pat[320];
        int r;
        snprintf(pat, sizeof pat, "%s*.*", dir);
        for (r = _dos_findfirst(pat, _A_NORMAL | _A_RDONLY | _A_ARCH | _A_HIDDEN | _A_SYSTEM, &f), i = 0; !r && i < 1024;
             r = _dos_findnext(&f), ++i)
            if ((lay = PADMAP_Find(PADMAP_FileId(f.name, f.size))) >= 0)
            {
                if (where) snprintf(where, wherelen, "%s", f.name);
                *how = 1;
                return lay;
            }
    }
#else
    {
        DIR* d = opendir(dir[0] ? dir : ".");
        struct dirent* e;
        for (i = 0; d && (e = readdir(d)) != NULL && i < 1024; ++i)
        {
            char fp[600];
            struct stat st;
            snprintf(fp, sizeof fp, "%s%s", dir, e->d_name);
            if (stat(fp, &st) || !S_ISREG(st.st_mode)) continue;
            if ((lay = PADMAP_Find(PADMAP_FileId(e->d_name, (uint32_t)st.st_size))) >= 0)
            {
                if (where) snprintf(where, wherelen, "%s", e->d_name);
                *how = 1;
                closedir(d);
                return lay;
            }
        }
        if (d) closedir(d);
    }
#endif
    return 0;
}
