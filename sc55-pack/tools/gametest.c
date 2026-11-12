/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* GAMETEST - runs DOS games with SBEMU's self-measurement (SBDIAG) and collects the results.
 *
 * Menu program: pick the games (Duke Nukem 3D, Doom, Doom II, one more of your choice), the test length
 * and which SBEMU options to try. Each test sets SBEMU's options (by running SBEMU again - the resident
 * copy takes them over), starts the measurement, runs the game, then reads the report.
 *   - Doom / Doom II play their built-in demo (-playdemo demo1) and quit by themselves.
 *   - Duke 3D runs its own demo playback (with the level music); when the time is up SBEMU types the quit keys
 *     (Esc for the menu, Q "quit?", Y, Y for the TEN screen; twice in case the first was too early),
 *     so it runs unattended too. Keys are per game (S / Q in the menu); a game without quit keys has to
 *     be quit by hand. The measurement always stops by itself after the test length.
 * Results: GAMETEST.TXT (summary table) and GTFULL.TXT (the full SBDIAG report of every test).
 * GTBEST.CFG keeps, per game, the options that gave the best music clock; SC55MENU plays the game with them.
 * If the PC locks up during a test, start GAMETEST again: it reports which test it was and can carry on.
 *
 * Needs SBEMU 1.0.0-beta.25-sc55 or newer (key typing), loaded with SC55 (or SC55JEM / SC55QMM).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <conio.h>
#include <pc.h>
#include "tui.h"
#include "form3d.h"
#include "safewr.h"
#include "mpuport.h"
#include <unistd.h>
#include <dir.h>
#include <stdarg.h>

static int MPU = 0x330;   /* SBEMU's MPU-401: found at start (mpu_find), any port */
#define NGAMES 4
#define NOPTS 4

/* keys: typed into the game by SBEMU. start = right after the game starts; quit = when the test time is up.
 * Words: W<sec> wait (W2, W0.5), ESC ENTER SPACE TAB BKSP UP DOWN LEFT RIGHT F1..F12 A..Z 0..9 */
typedef struct { char name[24]; char path[80]; char args[64]; int on; int autoquit; char kstart[64]; char kquit[64]; } GAME;
static GAME g_games[NGAMES] = {
    { "Duke Nukem 3D", "C:\\DUKE3D\\DUKE3D.EXE", "", 1, 0, "", "ESC W3 Q W2 Y W3 Y W3 ESC W3 Q W2 Y W3 Y" },
    { "Doom",          "C:\\DOOM\\DOOM.EXE", "-playdemo demo1", 1, 1, "", "" },
    { "Doom II",       "C:\\DOOM2\\DOOM2.EXE", "-playdemo demo1", 1, 1, "", "" },
    { "Other game",    "", "", 0, 0, "", "" },
};

typedef struct { const char* label; const char* sw; int nval; int vals[4]; const char* names[4]; unsigned sel; unsigned all; } OPT;
static OPT g_opts[NOPTS] = {
    { "Render-ahead buffer", "/VSCAHEAD", 4, {0, 25, 50, 100}, {"off", "25ms", "50ms", "100ms"}, 1u << 2, 0xF },
    { "Timer give-back",     "/VTIMER",   2, {1, 0},           {"on", "off"},                    1u << 0, 0x3 },
    { "SC-55 reverb/chorus", "/VSCFX",    2, {1, 0},           {"on", "off"},                    1u << 0, 0x3 },
    { "Speed control",       "/VSCAUTO",  2, {1, 0},           {"on", "off"},                    1u << 0, 0x3 },
};
static int g_secs = 40;
static char g_home[260];

/* ---------------------------------------------------------------- SBEMU side channel */
enum { F_MAGIC, F_VER, F_KHZ, F_MS, F_ISRN, F_ISRMS, F_ISRMAX, F_H1, F_H2, F_H5, F_H10, F_STRMAX, F_STRN,
       F_PITDIV, F_PITMAX, F_PITN, F_LOSTM, F_RENN, F_RENMS, F_RENMAX, F_FRAMES, F_FREQ, F_SYNTH,
       F_VIRQN, F_VIRQMS, F_VIRQMAX, F_TMPU, F_TSB, F_TOPL, F_TPIT, F_TPIC, F_TOTH, F_MIDI, F_MIDIDROP,
       F_GOV, F_FX, F_VOICES, F_FRMAX, F_PITFAST, F_EXPM, F_FIFOMIN, F_UNDER, F_REPLAY, F_BEXP, F_BGOT, F_COUNT };

static void mpu_cmd(int c) { outportb(MPU + 1, c); }
static int sbemu_present(void)
{
    int p = mpu_find(MPU);
    if (p) MPU = p;
    return p != 0;
}

/* ---------------------------------------------------------------- the game's own sound settings
 * SBEMU's Sound Blaster can be at any port / IRQ / DMA and its MPU-401 at any port, so before each test
 * the game's sound set-up is made to match what SBEMU is really using (BLASTER, which SBEMU writes,
 * and the port the MPU-401 answered at). Only the port / IRQ / DMA lines already in the file are
 * changed - the music and sound devices the game was set to stay as they are. The first time a file
 * is changed the original is kept as <name>.GTB.
 *   Doom engine (Doom, Doom II, Heretic, Hexen, Final Doom) DEFAULT.CFG: snd_sbport snd_sbirq snd_sbdma snd_mport
 *   Apogee/Build (Duke Nukem 3D, Shadow Warrior, Blood ...) *.CFG: BlasterAddress BlasterInterrupt
 *     BlasterDma8 BlasterDma16 MidiPort */
static void appendf(const char* file, const char* fmt, ...);
typedef struct { int a, i, d, h, p, ok; } SBVALS;
static SBVALS sb_values(void)
{
    SBVALS v = { 0x220, 7, 1, 5, 0x330, 0 };
    const char* b = getenv("BLASTER"); const char* s;
    if (b)
        for (s = b; *s; s++)
        {
            if (s != b && s[-1] != ' ') continue;
            switch (toupper((unsigned char)*s))
            {
            case 'A': v.a = (int)strtol(s + 1, NULL, 16); v.ok = 1; break;
            case 'I': v.i = atoi(s + 1); break;
            case 'D': v.d = atoi(s + 1); break;
            case 'H': v.h = atoi(s + 1); break;
            }
        }
    v.p = MPU;
    return v;
}
/* the new value written the way the old one was: "0x220", "220" (hex) or "544" (decimal) */
static void fmt_like(char* out, const char* old, int val, int is_port)
{
    while (*old == ' ' || *old == '\t' || *old == '=') old++;
    if (!is_port) sprintf(out, "%d", val);
    else if (old[0] == '0' && (old[1] == 'x' || old[1] == 'X')) sprintf(out, "0x%X", val);
    else if (atoi(old) >= 0x200) sprintf(out, "%d", val);        /* Doom writes ports in decimal: 544 */
    else sprintf(out, "%X", val);
}
/* returns how many lines were changed (-1: could not write) */
static int cfg_patch(const char* path, const SBVALS* v)
{
    static const struct { const char* key; int what; int port; } keys[] = {
        { "snd_sbport", 'A', 1 }, { "snd_sbirq", 'I', 0 }, { "snd_sbdma", 'D', 0 }, { "snd_mport", 'P', 1 },
        { "BlasterAddress", 'A', 1 }, { "BlasterInterrupt", 'I', 0 }, { "BlasterDma8", 'D', 0 },
        { "BlasterDma16", 'H', 0 }, { "MidiPort", 'P', 1 },
    };
    static char lines[600][160];
    char line[160], bak[300], tmp[310], val[24]; FILE* f; int n = 0, k, changed = 0, i;
    if ((f = fopen(path, "r")) == NULL) return 0;
    while (n < 600 && fgets(line, sizeof line, f)) { line[strcspn(line, "\r\n")] = 0; strcpy(lines[n++], line); }
    fclose(f);
    for (i = 0; i < n; i++)
        for (k = 0; k < (int)(sizeof keys / sizeof keys[0]); k++)
        {
            size_t kl = strlen(keys[k].key);
            const char* l = lines[i]; const char* rest;
            while (*l == ' ' || *l == '\t') l++;
            if (strncasecmp(l, keys[k].key, kl) || (l[kl] != ' ' && l[kl] != '\t' && l[kl] != '=')) continue;
            rest = l + kl;
            {
                int want = keys[k].what == 'A' ? v->a : keys[k].what == 'I' ? v->i : keys[k].what == 'D' ? v->d
                         : keys[k].what == 'H' ? v->h : v->p;
                const char* vs = rest; char sep[16]; int sl;
                while (*vs == ' ' || *vs == '\t' || *vs == '=') vs++;
                sl = (int)(vs - rest); if (sl > 15) sl = 15;
                memcpy(sep, rest, sl); sep[sl] = 0;
                fmt_like(val, vs, want, keys[k].port);
                if (!strcasecmp(vs, val)) break;
                snprintf(line, sizeof line, "%.*s%s%s", (int)(l - lines[i]) + (int)kl, lines[i], sep, val);
                strcpy(lines[i], line); changed++;
            }
            break;
        }
    if (!changed) return 0;
    strcpy(bak, path); { char* dot = strrchr(bak, '.'); if (dot) strcpy(dot, ".GTB"); else strcat(bak, ".GTB"); }
    if (access(bak, F_OK) != 0)
    {
        FILE* in = fopen(path, "rb"); FILE* out = in ? fopen(bak, "wb") : NULL; int c;
        if (out) { while ((c = fgetc(in)) != EOF) fputc(c, out); fclose(out); }
        if (in) fclose(in);
    }
    if ((f = sw_open(path, tmp, "w")) == NULL) return -1;
    for (i = 0; i < n; i++) fprintf(f, "%s\n", lines[i]);
    return sw_close(f, tmp, path) ? changed : -1;
}
/* every *.CFG in the game's folder; the summary goes to GTSTATE.TXT and *note */
static void game_sound_setup(const GAME* g, char* note, int nlen)
{
    char dir[260], pat[280], p[300]; struct ffblk fb; int done, total = 0, files = 0; char* bs;
    SBVALS v = sb_values();
    note[0] = 0;
    strcpy(dir, g->path); bs = strrchr(dir, '\\'); if (!bs) return; *bs = 0;
    sprintf(pat, "%s\\*.CFG", dir);
    for (done = findfirst(pat, &fb, 0); !done; done = findnext(&fb))
    {
        int c;
        sprintf(p, "%s\\%s", dir, fb.ff_name);
        c = cfg_patch(p, &v);
        if (c > 0) { total += c; files++; appendf("GTSTATE.TXT", "STEP %s: sound set to SB %X I%d D%d H%d, MIDI %X (%d lines)\n", p, v.a, v.i, v.d, v.h, v.p, c); }
        if (c < 0) appendf("GTSTATE.TXT", "STEP %s: could not be changed\n", p);
    }
    if (files) snprintf(note, nlen, "Game set to SB %X/%d/%d, MIDI %X (%s).", v.a, v.i, v.d, v.p, files == 1 ? "1 file" : "files");
}
static int read_report(unsigned long* d)
{
    unsigned char b[F_COUNT * 4];
    int i, j;
    mpu_cmd(0xA6);
    for (i = 0; i < F_COUNT * 4; ++i) b[i] = inportb(MPU);
    for (i = 0; i < F_COUNT; ++i) { d[i] = 0; for (j = 3; j >= 0; --j) d[i] = (d[i] << 8) | b[4 * i + j]; }
    return d[F_MAGIC] == 0x47444253UL && d[F_VER] >= 5;
}

/* ---------------------------------------------------------------- key scripts */
typedef struct { const char* n; int code; } KEYNAME;
static const KEYNAME g_keys[] = {
    {"ESC",1},{"BKSP",0x0E},{"TAB",0x0F},{"ENTER",0x1C},{"CTRL",0x1D},{"SHIFT",0x2A},{"ALT",0x38},{"SPACE",0x39},
    {"F1",0x3B},{"F2",0x3C},{"F3",0x3D},{"F4",0x3E},{"F5",0x3F},{"F6",0x40},{"F7",0x41},{"F8",0x42},{"F9",0x43},{"F10",0x44},
    {"F11",0x57},{"F12",0x58},{"UP",0x148},{"DOWN",0x150},{"LEFT",0x14B},{"RIGHT",0x14D},{"-",0x0C},{"=",0x0D},{NULL,0} };
static const char g_row1[] = "QWERTYUIOP", g_row2[] = "ASDFGHJKL", g_row3[] = "ZXCVBNM";

/* text -> SBEMU key script bytes; returns the length, or -1 and the bad word in *bad */
static int compile_keys(const char* text, unsigned char* out, int max, char* bad)
{
    char w[16]; int n = 0, i, len;
    const char* p = text;
    for (;;)
    {
        int code = -1;
        while (*p == ' ' || *p == ',') ++p;
        if (!*p) break;
        for (len = 0; p[len] && p[len] != ' ' && p[len] != ','; ++len) {}
        if (len > 15) len = 15;
        for (i = 0; i < len; ++i) w[i] = (char)toupper((unsigned char)p[i]);
        w[len] = 0; p += len;
        if (w[0] == 'W' && len > 1 && (isdigit((unsigned char)w[1]) || w[1] == '.'))
        {
            int t = (int)(atof(w + 1) * 10 + 0.5);
            while (t > 0 && n + 2 <= max) { int k = t > 250 ? 250 : t; out[n++] = 0x80; out[n++] = (unsigned char)k; t -= k; }
            continue;
        }
        if (len == 1 && w[0] >= '1' && w[0] <= '9') code = 2 + (w[0] - '1');
        else if (len == 1 && w[0] == '0') code = 0x0B;
        else if (len == 1 && strchr(g_row1, w[0])) code = 0x10 + (int)(strchr(g_row1, w[0]) - g_row1);
        else if (len == 1 && strchr(g_row2, w[0])) code = 0x1E + (int)(strchr(g_row2, w[0]) - g_row2);
        else if (len == 1 && strchr(g_row3, w[0])) code = 0x2C + (int)(strchr(g_row3, w[0]) - g_row3);
        else for (i = 0; g_keys[i].n; ++i) if (!strcmp(w, g_keys[i].n)) { code = g_keys[i].code; break; }
        if (code < 0) { if (bad) strcpy(bad, w); return -1; }
        if (code & 0x100) { if (n + 2 > max) break; out[n++] = 0x83; out[n++] = (unsigned char)code; }
        else { if (n + 1 > max) break; out[n++] = (unsigned char)code; }
    }
    return n;
}

static int game_auto(const GAME* g) { return g->autoquit || g->kquit[0]; }

/* send the game's key script to SBEMU (start keys, wait for the end of the test, quit keys) */
static void send_keys(const GAME* g)
{
    unsigned char a[96], b[96]; int na, nb, i;
    na = compile_keys(g->kstart, a, 40, NULL); if (na < 0) na = 0;
    nb = compile_keys(g->kquit, b, 40, NULL);  if (nb < 0) nb = 0;
    if (!na && !nb) return;
    mpu_cmd(0xAB);
    for (i = 0; i < na; ++i) mpu_cmd(a[i]);
    if (nb) { mpu_cmd(0x81); for (i = 0; i < nb; ++i) mpu_cmd(b[i]); }
    mpu_cmd(0x00);
}

/* ---------------------------------------------------------------- files */
static void path_in_home(char* out, const char* name) { sprintf(out, "%s\\%s", g_home, name); }

static void appendf(const char* file, const char* fmt, ...)
{
    char p[300]; FILE* f; va_list ap;
    path_in_home(p, file);
    f = fopen(p, "a");
    if (!f) return;
    va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fflush(f); fsync(fileno(f)); fclose(f);           /* on disk now, in case the next test locks up */
}

static void save_cfg(void)
{
    char p[300], tmp[310]; FILE* f; int i;
    path_in_home(p, "GAMETEST.CFG");
    f = sw_open(p, tmp, "w"); if (!f) return;
    fprintf(f, "SECS=%d\n", g_secs);
    for (i = 0; i < NGAMES; ++i) fprintf(f, "GAME%d=%d|%s|%s\n", i, g_games[i].on, g_games[i].path, g_games[i].args);
    for (i = 0; i < NGAMES; ++i) fprintf(f, "KEYS%d=%s|%s\n", i, g_games[i].kstart, g_games[i].kquit);
    for (i = 0; i < NOPTS; ++i) fprintf(f, "OPT%d=%u\n", i, g_opts[i].sel);
    sw_close(f, tmp, p);
}
static void load_cfg(void)
{
    char p[300], line[256]; FILE* f;
    path_in_home(p, "GAMETEST.CFG");
    f = fopen(p, "r"); if (!f) return;
    while (fgets(line, sizeof line, f))
    {
        int i; char* s; line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "SECS=", 5)) { g_secs = atoi(line + 5); if (g_secs < 20) g_secs = 20; if (g_secs > 255) g_secs = 255; }
        else if (sscanf(line, "GAME%d=", &i) == 1 && i >= 0 && i < NGAMES && (s = strchr(line, '=')) != NULL)
        {
            char* a = strchr(s + 1, '|'); char* b = a ? strchr(a + 1, '|') : NULL;
            if (a && b) { *a = 0; *b = 0; g_games[i].on = atoi(s + 1); strncpy(g_games[i].path, a + 1, 79); strncpy(g_games[i].args, b + 1, 63); }
        }
        else if (sscanf(line, "KEYS%d=", &i) == 1 && i >= 0 && i < NGAMES && (s = strchr(line, '=')) != NULL)
        {
            char* a = strchr(s + 1, '|');
            if (a) { *a = 0; strncpy(g_games[i].kstart, s + 1, 63); g_games[i].kstart[63] = 0; strncpy(g_games[i].kquit, a + 1, 63); g_games[i].kquit[63] = 0; }
            /* older defaults: start a new game (Duke's own demo playback is used now) and a quit that left
             * Duke waiting on its exit screen. Only replaced if they were never changed by hand. */
            if (a && i == 0 && !strcmp(g_games[0].kstart, "W15 ESC W2 ENTER W2 ENTER W2 ENTER")) g_games[0].kstart[0] = 0;
            /* Duke 3D's quit, worked out in QEMU with Atomic 1.5 and its source (MENUES.C, GAME.C): Esc opens the
             * main menu over the demo, Q there asks "quit?", Y says yes, any key (Y) closes the TEN screen. No
             * Enter: in a menu it would start a new game. The second round covers a first Esc that closed an
             * open menu or only got past the logo / title screen. */
            if (a && i == 0 && (!strcmp(g_games[0].kquit, "F10 W1 Y") || !strcmp(g_games[0].kquit, "F10 W1 Y W2 SPACE W2 SPACE")
                                || !strcmp(g_games[0].kquit, "F10 W1 Y W3 ESC W1 UP W1 ENTER W1 Y W3 SPACE W2 SPACE")
                                || !strcmp(g_games[0].kquit, "ESC W2 UP W1 ENTER W2 ENTER W3 ENTER W3 ENTER")
                                || !strcmp(g_games[0].kquit, "ESC W3 F10 W2 Y W3 ENTER W3 ESC W3 F10 W2 Y W3 ENTER W3 ENTER")))
                strcpy(g_games[0].kquit, "ESC W3 Q W2 Y W3 Y W3 ESC W3 Q W2 Y W3 Y");
        }
        else if (sscanf(line, "OPT%d=", &i) == 1 && i >= 0 && i < NOPTS && (s = strchr(line, '=')) != NULL)
        { unsigned v = (unsigned)atoi(s + 1) & g_opts[i].all; if (v) g_opts[i].sel = v; }
    }
    fclose(f);
}

/* ---------------------------------------------------------------- test plan */
typedef struct { int game; int v[NOPTS]; } RUN;
static RUN g_plan[1024];
static int g_nplan;

static int build_plan(int everything)
{
    int gi, idx[NOPTS], k;
    g_nplan = 0;
    for (gi = 0; gi < NGAMES; ++gi)
    {
        if (!g_games[gi].on || !g_games[gi].path[0]) continue;
        memset(idx, 0, sizeof idx);
        for (;;)
        {
            int ok = 1;
            for (k = 0; k < NOPTS; ++k)
            {
                unsigned m = everything ? g_opts[k].all : g_opts[k].sel;
                if (!(m & (1u << idx[k]))) { ok = 0; break; }
            }
            if (ok && g_nplan < 1024)
            {
                g_plan[g_nplan].game = gi;
                for (k = 0; k < NOPTS; ++k) g_plan[g_nplan].v[k] = g_opts[k].vals[idx[k]];
                ++g_nplan;
            }
            for (k = NOPTS - 1; k >= 0; --k) { if (++idx[k] < g_opts[k].nval) break; idx[k] = 0; }
            if (k < 0) break;
        }
    }
    return g_nplan;
}

static void run_label(const RUN* r, char* out)
{
    sprintf(out, "%-13s ahead %-3d timer %-3s fx %-3s auto %-3s", g_games[r->game].name, r->v[0],
            r->v[1] ? "on" : "off", r->v[2] ? "on" : "off", r->v[3] ? "on" : "off");
}

/* ---------------------------------------------------------------- SBEMU options */
static int set_sbemu(const int* v)
{
    char cmd[512], optf[300], opts[310];
    /* the saved settings as a response file (never through the DOS environment) */
    sprintf(optf, "%s\\SBOPTS.TXT", g_home);
    if (access(optf, R_OK) != 0) sprintf(optf, "%s\\SBOPTS.BAT", g_home);
    if (access(optf, R_OK) == 0) sprintf(opts, "@%s", optf); else opts[0] = 0;
    if (v)
        sprintf(cmd, "%s\\SBEMU.EXE /VMPU /VSCROM:ROMS %s /VSCAHEAD:%d /VTIMER:%d /VSCFX:%d /VSCAUTO:%d > NUL",
                g_home, opts, v[0], v[1], v[2], v[3]);
    else /* back to the user's own settings (defaults first, so SBOPTS wins) */
        sprintf(cmd, "%s\\SBEMU.EXE /VMPU /VSCROM:ROMS /VSCAHEAD:50 /VTIMER:1 /VSCFX:1 /VSCAUTO:1 %s > NUL",
                g_home, opts);
    /* DJGPP's system() handles the redirection itself and passes long command lines to SBEMU (a DJGPP
     * program) without the 126-character DOS limit */
    return system(cmd);
}

/* ---------------------------------------------------------------- best settings per game (GTBEST.CFG)
 * BEST<game>=<music clock x10>|/VSCAHEAD:50 /VTIMER:1 /VSCFX:1 /VSCAUTO:1
 * A new result wins if its clock is more than 0.2% closer to 100%, or about as close and nearer the defaults
 * (the defaults are the most tested; reverb/chorus on sounds better). */
static int  g_bestclk[NGAMES];
static char g_bestopt[NGAMES][120];

static void best_load(void)
{
    char p[300], line[200]; FILE* f; int i, clk; char* a;
    memset(g_bestclk, 0, sizeof g_bestclk); memset(g_bestopt, 0, sizeof g_bestopt);
    path_in_home(p, "GTBEST.CFG");
    if ((f = fopen(p, "r")) == NULL) return;
    while (fgets(line, sizeof line, f))
    {
        line[strcspn(line, "\r\n")] = 0;
        if (sscanf(line, "BEST%d=%d|", &i, &clk) == 2 && i >= 0 && i < NGAMES && (a = strchr(line, '|')) != NULL)
        { g_bestclk[i] = clk; strncpy(g_bestopt[i], a + 1, 119); }
    }
    fclose(f);
}
static void best_save(void)
{
    char p[300], tmp[310]; FILE* f; int i;
    path_in_home(p, "GTBEST.CFG");
    if ((f = sw_open(p, tmp, "w")) == NULL) return;
    for (i = 0; i < NGAMES; ++i) if (g_bestopt[i][0]) fprintf(f, "BEST%d=%d|%s\n", i, g_bestclk[i], g_bestopt[i]);
    sw_close(f, tmp, p);
}
static int defaults_score(const int* v) { return (v[0] == 50) + (v[1] == 1) + (v[2] == 1) + (v[3] == 1); }
static int defaults_score_str(const char* o)
{
    int v[4] = { -1, -1, -1, -1 };
    sscanf(o, "/VSCAHEAD:%d /VTIMER:%d /VSCFX:%d /VSCAUTO:%d", &v[0], &v[1], &v[2], &v[3]);
    return defaults_score(v);
}
static int best_update(int game, int clk10, const int* v)
{
    int better;
    if (clk10 <= 0) return 0;
    {   /* closest to 100.0% wins (over 100% means the game ran too fast, not better) */
        int dn = clk10 > 1000 ? clk10 - 1000 : 1000 - clk10;
        int dob = g_bestclk[game] > 1000 ? g_bestclk[game] - 1000 : 1000 - g_bestclk[game];
        if (!g_bestopt[game][0]) better = 1;
        else if (dn + 2 < dob) better = 1;
        else if (dn <= dob + 2 && defaults_score(v) > defaults_score_str(g_bestopt[game])) better = 1;
        else better = 0;
    }
    if (!better) return 0;
    g_bestclk[game] = clk10;
    sprintf(g_bestopt[game], "/VSCAHEAD:%d /VTIMER:%d /VSCFX:%d /VSCAUTO:%d", v[0], v[1], v[2], v[3]);
    best_save();
    return 1;
}

/* ---------------------------------------------------------------- one test */
static double clock_pct(const unsigned long* d) { return d[F_BEXP] > 20 ? 100.0 * d[F_BGOT] / d[F_BEXP] : 0; }

static void summary_line(const RUN* r, const unsigned long* d, char* out)
{
    double clk = clock_pct(d);
    double audio = d[F_FREQ] ? d[F_FRAMES] * 1000.0 / d[F_FREQ] : 0;
    double load = audio > 0 ? 100.0 * d[F_RENMS] / audio : 0;
    double hz = d[F_PITFAST] ? 1193182.0 / d[F_PITFAST] : 0;
    char lab[100], clks[16];
    run_label(r, lab);
    if (clk > 0) sprintf(clks, "%6.1f%%", clk); else strcpy(clks, "   n/a ");
    sprintf(out, "%s | %4lus %4.0fHz clock %s gave %5lu | int max %4.1f run %4.1f ms | synth %3.0f%% | silence %4.1fs drop %lu",
            lab, d[F_MS] / 1000, hz, clks, d[F_REPLAY], d[F_ISRMAX] / 1000.0, d[F_STRMAX] / 1000.0, load,
            d[F_FREQ] ? (double)d[F_UNDER] / d[F_FREQ] : 0, d[F_MIDIDROP]);
}

/* the "running" screen shown before each game starts */
static void draw_run(int i, int n, const RUN* r, const char* status, const char* note1, const char* note2)
{
    const GAME* g = &g_games[r->game];
    int left = (n - i) * (g_secs + 20);
    tui_clear(0x17);
    tui_fill(1, 1, 80, 1, 0x30, ' ');
    tui_put(2, 1, 0x3F, "GAMETEST");
    tui_put(11, 1, 0x30, "\xB3 running - keep your hands off the keyboard");
    tui_window(5, 4, 70, 14, 0x1F, "Test");
    tui_printf(8, 6, 0x1E, "Test %d of %d", i + 1, n);
    tui_printf(25, 6, 0x17, "about %d min left", (left + 59) / 60);
    tui_bar(8, 7, 62, i, n, 0x1A, 0x18);
    tui_printf(8, 9, 0x1F, "Game      %s  (%d s)", g->name, g_secs);
    tui_printf(8, 10, 0x1F, "Options   render-ahead %d ms, timer give-back %s,", r->v[0], r->v[1] ? "on" : "off");
    tui_printf(8, 11, 0x1F, "          reverb/chorus %s, speed control %s", r->v[2] ? "on" : "off", r->v[3] ? "on" : "off");
    if (g_bestopt[r->game][0])
        tui_printf(8, 12, 0x17, "Best so far for this game: music clock %d.%d%%", g_bestclk[r->game] / 10, g_bestclk[r->game] % 10);
    tui_putw(8, 14, 64, 0x1E, status);
    if (note1) tui_putw(8, 15, 64, 0x17, note1);
    if (note2) tui_putw(8, 16, 64, 0x17, note2);
    tui_fill(1, 25, 80, 1, 0x30, ' ');
    tui_put(2, 25, 0x30, "Esc Esc Esc (in a game) stops the run.  Locked up? Restart, run GAMETEST.");
    tui_flush();
}

static int g_stopped;   /* Esc Esc Esc during a game */
static int run_one(int i, int n)
{
    const RUN* r = &g_plan[i];
    const GAME* g = &g_games[r->game];
    char dir[260], *bs, cmd[640], lab[100], line[300], n1[120], n2[120];
    unsigned long d[F_COUNT];

    run_label(r, lab);
    draw_run(i, n, r, "Setting SBEMU options ...", NULL, NULL);
    set_sbemu(r->v);
    tui_invalidate();
    if (!sbemu_present()) { draw_run(i, n, r, "SBEMU's MPU-401 does not answer: run SC55 first.", "Press a key.", NULL); tui_key(); return -1; }
    {   /* the game's sound set-up matched to SBEMU's port / IRQ / DMA and MPU-401 port */
        char note[100];
        game_sound_setup(g, note, sizeof note);
        if (note[0]) { draw_run(i, n, r, "Setting the game's sound ...", note, NULL); sleep(1); }
    }

    if (!game_auto(g))
    {
        sprintf(n1, "%s does not quit by itself: play with music for %d s, then quit.", g->name, g_secs);
        draw_run(i, n, r, "Press a key to start the game, Esc to skip this test.", n1, "(The measurement stops by itself after the test length.)");
        if (tui_key() == K_ESC) { appendf("GAMETEST.TXT", "%s | skipped\n", lab); return 0; }
    }

    appendf("GTSTATE.TXT", "START %d\n", i);
    /* measure for the test length, then keep the report */
    mpu_cmd(0xA9); mpu_cmd(g_secs > 255 ? 255 : g_secs);
    send_keys(g);
    mpu_cmd(0xAD);                /* SBEMU watches for Esc Esc Esc: stop the run */
    mpu_cmd(0xA7);
    appendf("GTSTATE.TXT", "STEP measuring started, keys sent, starting the game\n");
    if (g->kstart[0] || g->kquit[0])
    {
        sprintf(n1, "SBEMU types: %.24s ... at the end: %.16s", g->kstart[0] ? g->kstart : "-", g->kquit[0] ? g->kquit : "-");
        draw_run(i, n, r, "Starting the game ...", n1, "If the game does not quit by itself, quit it by hand.");
        sleep(2);
    }
    else draw_run(i, n, r, "Starting the game ...", NULL, NULL);

    strcpy(dir, g->path);
    bs = strrchr(dir, '\\');
    if (bs) { *bs = 0; if (strlen(dir) == 2 && dir[1] == ':') strcat(dir, "\\"); }
    sprintf(cmd, "%s %s", g->path, g->args);
    if (bs) { setdisk(toupper(dir[0]) - 'A'); chdir(dir); }
    tui_done();                                   /* the game gets a normal 80x25 DOS screen */
    system(cmd);
    appendf("GTSTATE.TXT", "STEP the game quit and returned to GAMETEST\n");
    mpu_cmd(0xAC); /* no more keys */
    {
        unsigned char e0, e1;
        mpu_cmd(0xAF); e0 = inportb(MPU); e1 = inportb(MPU);
        mpu_cmd(0xAE);
        g_stopped = (e0 == 'E' && e1 == 1);
    }
    while (kbhit()) getch();
    setdisk(toupper(g_home[0]) - 'A'); chdir(g_home);
    textmode(C80); tui_set_saved_mode(); _setcursortype(_NOCURSOR); tui_apply_theme(); tui_font_loaded = 0; tui_font_apply(); tui_invalidate();   /* the game changed the video mode */

    mpu_cmd(0xAA); /* stop and keep the report */
    if (!read_report(d))
    {
        appendf("GAMETEST.TXT", "%s | no report (SBEMU older than beta.24?)\n", lab);
        appendf("GTSTATE.TXT", "DONE %d\n", i);
        return 0;
    }
    appendf("GTSTATE.TXT", "STEP report read from SBEMU\n");
    summary_line(r, d, line);
    appendf("GAMETEST.TXT", "%s\n", line);
    {
        int clk10 = (int)(clock_pct(d) * 10 + 0.5), nb = best_update(r->game, clk10, r->v);
        sprintf(n1, "Music clock %d.%d%%%s", clk10 / 10, clk10 % 10, nb ? "  - new best for this game" : "");
        sprintf(n2, "Writing the full report to GTFULL.TXT ...");
        draw_run(i + 1, n, r, "Test finished.", n1, n2);
    }
    appendf("GTFULL.TXT", "\n######## Test %d: %s\n", i + 1, lab);
    sprintf(cmd, "%s\\SBDIAG.EXE >> %s\\GTFULL.TXT", g_home, g_home);
    appendf("GTSTATE.TXT", "STEP running SBDIAG for GTFULL.TXT\n");
    system(cmd);
    tui_invalidate();
    appendf("GTSTATE.TXT", "DONE %d\n", i);
    return 1;
}

/* green: music clock 99-100%, yellow 95-99%, red below 95% or a lock-up */
/* green 99-101%, yellow 95-99 / 101-105%, red otherwise (clock x10) */
static int clk_attr(int c, int bg) { int k = (c >= 990 && c <= 1010) ? 0x0A : (c >= 950 && c <= 1050) ? 0x0E : 0x0C; return (bg << 4) | k; }

static int line_colour(const char* s)
{
    const char* c = strstr(s, "clock ");
    if (strstr(s, "LOCKED") || strstr(s, "no report")) return 0x0C;
    if (strstr(s, "skipped")) return 0x08;
    if (c)
    {
        double v;
        if (sscanf(c + 6, " %lf%%", &v) == 1) return clk_attr((int)(v * 10 + 0.5), 0);
    }
    if (!strncmp(s, "====", 4)) return 0x0B;
    return 0x07;
}
static void show_file(const char* name)
{
    char p[300];
    path_in_home(p, name);
    tui_view_file(p, line_colour);
}

/* after a run: the best settings per game */
static void show_best(void)
{
    int i, y = 7;
    tui_clear(0x17);
    tui_fill(1, 1, 80, 1, 0x30, ' ');
    tui_put(2, 1, 0x3F, "GAMETEST");
    tui_put(11, 1, 0x30, "\xB3 best settings found");
    tui_window(4, 4, 73, 14, 0x1F, "Best settings per game (used by MENU's Play)");
    for (i = 0; i < NGAMES; ++i)
    {
        if (!g_games[i].path[0]) continue;
        tui_printf(7, y, 0x1E, "%-14s", g_games[i].name);
        if (g_bestopt[i][0])
        {
            int c = g_bestclk[i];
            tui_printf(22, y, clk_attr(c, 1), "%3d.%d%%", c / 10, c % 10);
            tui_printf(31, y, 0x1F, "%.42s", g_bestopt[i]);
        }
        else tui_put(22, y, 0x18, "not tested yet");
        y += 2;
    }
    tui_put(7, 16, 0x17, "Clock 99-100% = right tempo. MENU's Play uses these automatically.");
    tui_fill(1, 25, 80, 1, 0x30, ' ');
    tui_put(2, 25, 0x30, "Any key: back");
    tui_key();
}

static void run_plan(int from, int n)
{
    int i, r = 1;
    char hdr[200];
    if (from == 0)
    {
        char p[300];
        path_in_home(p, "GTSTATE.TXT"); remove(p);
        appendf("GTSTATE.TXT", "PLAN %d\n", n);
        for (i = 0; i < n; ++i)
            appendf("GTSTATE.TXT", "RUN %d %d %d %d %d %d\n", i, g_plan[i].game, g_plan[i].v[0], g_plan[i].v[1], g_plan[i].v[2], g_plan[i].v[3]);
        sprintf(hdr, "==== GAMETEST: %d tests, %d s each ====\n", n, g_secs);
        appendf("GAMETEST.TXT", "%s", hdr);
        appendf("GTFULL.TXT", "%s", hdr);
    }
    g_stopped = 0;
    for (i = from; i < n && r >= 0; ++i)
    {
        r = run_one(i, n);
        if (g_stopped) { appendf("GAMETEST.TXT", "==== stopped with Esc Esc Esc after test %d of %d ====\n", i + 1, n); break; }
        if (kbhit() && getch() == 27 && tui_yesno("Stop the remaining tests?")) break;
    }
    set_sbemu(NULL);
    tui_invalidate();
    appendf("GTSTATE.TXT", "END\n");
    show_best();
    show_file("GAMETEST.TXT");
}

/* after a lock-up: the plan is in GTSTATE.TXT, find the test that started but never finished */
static void check_resume(void)
{
    char p[300], line[128], step[100] = "starting the test (setting SBEMU options)"; FILE* f; int started = -1, done = -1, ended = 0, n = 0, k;
    path_in_home(p, "GTSTATE.TXT");
    f = fopen(p, "r"); if (!f) return;
    while (fgets(line, sizeof line, f))
    {
        int a, b, c, dd, e, gg;
        if (sscanf(line, "PLAN %d", &a) == 1) { n = a < 1024 ? a : 1024; }
        else if (sscanf(line, "RUN %d %d %d %d %d %d", &a, &gg, &b, &c, &dd, &e) == 6 && a >= 0 && a < 1024)
        { g_plan[a].game = gg; g_plan[a].v[0] = b; g_plan[a].v[1] = c; g_plan[a].v[2] = dd; g_plan[a].v[3] = e; }
        else if (sscanf(line, "START %d", &a) == 1) { started = a; strcpy(step, "starting the test (setting SBEMU options)"); }
        else if (!strncmp(line, "STEP ", 5)) { line[strcspn(line, "\r\n")] = 0; strncpy(step, line + 5, sizeof step - 1); step[sizeof step - 1] = 0; }
        else if (sscanf(line, "DONE %d", &a) == 1) done = a;
        else if (!strncmp(line, "END", 3)) ended = 1;
    }
    fclose(f);
    if (ended || started < 0 || started == done || n == 0) return;
    g_nplan = n;
    {
        char lab[100];
        run_label(&g_plan[started], lab);
        appendf("GAMETEST.TXT", "%s | LOCKED UP after: %s\n", lab, step);
        appendf("GTSTATE.TXT", "DONE %d\n", started);
        tui_clear(0x17);
        tui_fill(1, 1, 80, 1, 0x4F, ' ');
        tui_put(2, 1, 0x4F, "GAMETEST \xB3 the last run did not finish");
        tui_window(4, 4, 73, 12, 0x4F, "Lock-up");
        tui_printf(7, 6, 0x4F, "Test %d of %d locked up the PC:", started + 1, n);
        tui_printf(7, 7, 0x4E, "%.66s", lab);
        tui_printf(7, 9, 0x4F, "Last step reached:");
        tui_printf(7, 10, 0x4E, "%.66s", step);
        tui_put(7, 12, 0x4F, "Written to GAMETEST.TXT - please send me that line.");
        k = started + 1;
        if (k < n)
        {
            char q[80];
            sprintf(q, "Carry on with the remaining %d tests?", n - k);
            if (tui_yesno(q)) { run_plan(k, n); return; }
        }
        else { tui_put(7, 13, 0x4F, "It was the last test. Press a key."); tui_key(); }
        appendf("GTSTATE.TXT", "END\n");
    }
}

/* ---------------------------------------------------------------- main menu */
/* rows: 0..3 games, 4..7 options, 8 length, 9 run, 10 run all, 11 results, 12 full reports, 13 best settings, 14 clear */
#define ROWS 15
static const char* g_hint[ROWS] = {
    "Space on/off   E path   A command-line options   S start keys   Q quit keys",
    "Space on/off   E path   A command-line options   S start keys   Q quit keys",
    "Space on/off   E path   A command-line options   S start keys   Q quit keys",
    "Space on/off   E path   A command-line options   S start keys   Q quit keys",
    "Left/Right pick a value, Space switches it on/off. Every combination is tested.",
    "Left/Right pick a value, Space switches it on/off. Every combination is tested.",
    "Left/Right pick a value, Space switches it on/off. Every combination is tested.",
    "Left/Right pick a value, Space switches it on/off. Every combination is tested.",
    "Left/Right: 5 s shorter / longer. Use 40 s or more: games take a few s to load.",
    "Runs the ticked games with every combination of the ticked option values.",
    "Runs every option value with every ticked game: thorough, but takes long.",
    "GAMETEST.TXT: one line per test, the music clock in colour. Right arrow: the rest of each line.",
    "GTFULL.TXT: the complete SBDIAG report of every test (why music was slow, CPU load).",
    "The settings that gave each game the best music clock (MENU's Play uses them).",
    "Starts over: deletes GAMETEST.TXT, GTFULL.TXT and the best settings.",
};

static void draw_menu(int row, const int* col)
{
    int i, k, y, np, na, HL = TUI_FANCY ? 0x5F : 0x70;
    char buf[200];
    na = build_plan(1); np = build_plan(0);
    tui_clear(0x10);
    tui_fill(1, 1, 80, 1, 0x30, ' ');
    tui_put(2, 1, 0x3F, "GAMETEST");
    tui_put(11, 1, 0x30, "\xB3 SBEMU game test with SBDIAG");
    tui_printf(64, 1, 0x30, "style: %s", tui_theme_name[tui_theme]);

    tui_panel(2, 2, 78, 8, 1, 0x1E, "Games");
    for (i = 0; i < NGAMES; ++i)
    {
        const GAME* g = &g_games[i];
        int a = row == i ? HL : 0x1F;
        y = 3 + i;
        snprintf(buf, sizeof buf, " [%c] %-14s %-26.26s %-12s", g->on ? 'x' : ' ', g->name, g->path[0] ? g->path : "(not set)",
                 g->autoquit ? "quits itself" : g->kquit[0] ? "auto (keys)" : "you quit");
        tui_putw(4, y, 63, a, buf);
        if (g_bestopt[i][0]) tui_printf(67, y, row == i ? HL : clk_attr(g_bestclk[i], 1), " best %3d.%d%%", g_bestclk[i] / 10, g_bestclk[i] % 10);
        else tui_putw(67, y, 12, row == i ? HL : 0x18, "   untested");
    }
    {
        const GAME* g = &g_games[row < 4 ? row : 0];
        snprintf(buf, sizeof buf, "start keys: %s%s%s", g->kstart[0] ? g->kstart : "- (none)",
                 g->args[0] ? "    options: " : "", g->args);
        tui_putw(5, 7, 74, 0x1B, buf);
        snprintf(buf, sizeof buf, "quit keys:  %s", g->kquit[0] ? g->kquit : (g->autoquit ? "- (quits by itself)" : "- (you quit)"));
        tui_putw(5, 8, 74, 0x1B, buf);
        if (row >= 4) { tui_putw(5, 7, 74, 0x1B, ""); tui_putw(5, 8, 74, 0x1B, ""); }
    }

    tui_panel(2, 10, 78, 6, 1, 0x1E, "SBEMU options to test");
    for (k = 0; k < NOPTS; ++k)
    {
        int x;
        y = 11 + k;
        x = 4 + tui_printf(4, y, row == 4 + k ? 0x1E : 0x17, " %-21s %-10s", g_opts[k].label, g_opts[k].sw);
        for (i = 0; i < g_opts[k].nval; ++i)
        {
            int on = (g_opts[k].sel >> i) & 1;
            int a = row == 4 + k && col[k] == i ? HL : (on ? 0x1A : 0x18);
            x += tui_printf(x, y, a, " [%c]%-6s", on ? 'x' : ' ', g_opts[k].names[i]);
        }
    }

    tui_panel(2, 16, 78, 8, 1, 0x1E, "Run");
    snprintf(buf, sizeof buf, " Test length per game:  %3d s   \x1B \x1A", g_secs);
    tui_putw(4, 17, 74, row == 8 ? HL : 0x1F, buf);
    snprintf(buf, sizeof buf, " Run the selected tests          %3d tests, about %d min", np, (np * (g_secs + 20) + 59) / 60);
    tui_putw(4, 18, 74, row == 9 ? HL : 0x1F, buf);
    snprintf(buf, sizeof buf, " Test ALL options                %3d tests, about %d min", na, (na * (g_secs + 20) + 59) / 60);
    tui_putw(4, 19, 74, row == 10 ? HL : 0x1F, buf);
    tui_putw(4, 20, 37, row == 11 ? HL : 0x1F, " View results (GAMETEST.TXT)");
    tui_putw(4, 21, 37, row == 12 ? HL : 0x1F, " Full SBDIAG reports (GTFULL.TXT)");
    tui_putw(41, 20, 37, row == 13 ? HL : 0x1F, " Best settings per game");
    tui_putw(41, 21, 37, row == 14 ? HL : 0x1F, " Clear all results");
    {   /* the values SBEMU is using now; Doom-engine and Apogee/Build games are set to them by GAMETEST */
        SBVALS v = sb_values();
        snprintf(buf, sizeof buf, "Games get SB %X/%d/%d + MIDI %X automatically (Doom, Duke 3D ...).", v.a, v.i, v.d, v.p);
        tui_put(5, 22, 0x13, buf);
    }

    tui_fill(1, 24, 80, 1, 0x10, ' ');
    tui_putw(2, 24, 78, 0x1E, g_hint[row]);
    tui_fill(1, 25, 80, 1, 0x30, ' ');
    tui_put(2, 25, 0x30, "\x18\x19 move  Enter select  F3 style  F4 screen  Esc quit  Full reports: GTFULL.TXT");
}

/* ---------------------------------------------------------------- 3D screen (beta.77, form3d.h)
 * On while MENU3D.CFG exists (the 3D menu is on); F8 switches. Typing stays in 3D; the result viewers and the
 * test runs themselves are text screens (the 3D mode is left for them and set again afterwards). */
static int g3, g3_present = -1;
static double g3_present_t;
static F3VIEW g3_v;
static int g3_row[F3_MAXROWS];                 /* view row -> menu row (0..14), -1 = heading */
static void g3_build(F3VIEW* v, int row, const int* col)
{
    int i, k, np, na; char b[120];
    F3ROW* r;
    f3_clear(v);
    v->app = "GAMETEST"; v->accent = 0x20C080; v->ntab = 0;
    snprintf(v->title, sizeof v->title, "SBEMU game test with SBDIAG");
    if (g3_present < 0 || m3_now() - g3_present_t > 2) { g3_present = sbemu_present(); g3_present_t = m3_now(); }
    snprintf(v->status, sizeof v->status, "%s", g3_present ? "SBEMU loaded" : "SBEMU not loaded");
    v->statcol = g3_present ? 0x60FF80 : 0xFFB040;
    na = build_plan(1); np = build_plan(0);
#define G3ADD(kind, lab, val, mrow) (r = f3_add(v, kind, lab, val), g3_row[v->nrows - 1] = mrow, r)
    G3ADD(F3_HEAD, "Games", NULL, -1);
    for (i = 0; i < NGAMES; i++)
    {
        const GAME* g = &g_games[i];
        if (g_bestopt[i][0]) snprintf(b, sizeof b, "%s  best %d.%d%%", g->path[0] ? g->path : "(not set)", g_bestclk[i] / 10, g_bestclk[i] % 10);
        else snprintf(b, sizeof b, "%s  %s", g->path[0] ? g->path : "(not set)", g->autoquit ? "quits itself" : g->kquit[0] ? "auto (keys)" : "you quit");
        G3ADD(F3_ITEM, g->name, b, i);
        r->mark = g->on ? F3M_ON : F3M_OFF; r->dim = !g->path[0];
    }
    G3ADD(F3_HEAD, "SBEMU options to test", NULL, -1);
    for (k = 0; k < NOPTS; k++)
    {
        int n = 0;
        b[0] = 0;
        for (i = 0; i < g_opts[k].nval; i++)
            n += snprintf(b + n, sizeof b - n, "%s%s%s%s ", i == col[k] ? "[" : " ", (g_opts[k].sel >> i) & 1 ? "*" : "", g_opts[k].names[i], i == col[k] ? "]" : " ");
        G3ADD(F3_VALUE, g_opts[k].label, b, 4 + k);
    }
    G3ADD(F3_HEAD, "Run", NULL, -1);
    snprintf(b, sizeof b, "%d s", g_secs); G3ADD(F3_VALUE, "Test length per game", b, 8);
    snprintf(b, sizeof b, "%d tests, about %d min", np, (np * (g_secs + 20) + 59) / 60); G3ADD(F3_ITEM, "Run the selected tests", b, 9)->mark = F3M_GO;
    snprintf(b, sizeof b, "%d tests, about %d min", na, (na * (g_secs + 20) + 59) / 60); G3ADD(F3_ITEM, "Test ALL options", b, 10)->mark = F3M_GO;
    G3ADD(F3_ITEM, "View results", "GAMETEST.TXT", 11);
    G3ADD(F3_ITEM, "Full SBDIAG reports", "GTFULL.TXT", 12);
    G3ADD(F3_ITEM, "Best settings per game", "used by MENU's Play", 13);
    G3ADD(F3_ITEM, "Clear all results", "", 14);
#undef G3ADD
    for (i = 0; i < v->nrows; i++) if (g3_row[i] == row) v->sel = i;
    if (row < 4)
    {
        const GAME* g = &g_games[row];
        snprintf(v->info, sizeof v->info, "Space on/off   E path   A options   S start keys   Q quit keys.   Start keys: %s%s%s.   Quit keys: %s.",
                 g->kstart[0] ? g->kstart : "none", g->args[0] ? "   Options: " : "", g->args,
                 g->kquit[0] ? g->kquit : (g->autoquit ? "none (quits by itself)" : "none (you quit)"));
    }
    else if (row < 8) snprintf(v->info, sizeof v->info, "%s  (* = tested; Space switches the value in [ ])", g_hint[row]);
    else snprintf(v->info, sizeof v->info, "%s", g_hint[row]);
    { SBVALS sv = sb_values(); snprintf(v->foot, sizeof v->foot, "Games get SB %X/%d/%d + MIDI %X automatically (Doom, Duke 3D ...)", sv.a, sv.i, sv.d, sv.p); }
    v->keys = "Arrows move  Enter select  Space on/off  F3 style  F8 text  Esc quit";
}
static void ui_message(const char* t, const char* m) { if (g3) f3_message(&g3_v, t, m); else tui_message(t, m); }
static int ui_yesno(const char* q) { return g3 ? f3_yesno(&g3_v, q) : tui_yesno(q); }
static int ui_input(const char* t, const char* p, char* b, int max, int up) { return g3 ? f3_input(&g3_v, t, p, b, max, up) : tui_input(t, p, b, max, up); }
static void g3_text_begin(void) { if (g3) { f3_leave(); tui_init(); } }
static void g3_text_end(void) { if (g3) { tui_done(); if (!f3_enter()) { g3 = 0; tui_init(); } g3_present = -1; } }

static void clear_results(void)
{
    char p[300];
    if (!ui_yesno("Delete GAMETEST.TXT, GTFULL.TXT and the best settings?")) return;
    path_in_home(p, "GAMETEST.TXT"); remove(p);
    path_in_home(p, "GTFULL.TXT"); remove(p);
    path_in_home(p, "GTBEST.CFG"); remove(p);
    path_in_home(p, "GTSTATE.TXT"); remove(p);
    best_load();
}

int main(int argc, char** argv)
{
    int row = 0, col[NOPTS] = {0}, c, k;
    char* s;
    strcpy(g_home, argv[0]);
    for (s = g_home; *s; ++s) if (*s == '/') *s = '\\';
    s = strrchr(g_home, '\\'); if (s) *s = 0; else getcwd(g_home, sizeof g_home);
    load_cfg();
    best_load();
    tui_theme_load(g_home);
    tui_init();
    check_resume();
    /* GAMETEST /RUN: run the ticked tests straight away (no questions), then show the menu */
    for (k = 1; k < argc; k++)
        if (!strcasecmp(argv[k], "/RUN"))
        {
            int n = build_plan(0);
            if (n && sbemu_present()) run_plan(0, n);
            else ui_message("GAMETEST", n ? "SBEMU's MPU-401 does not answer: run SC55 first." : "No game selected (or no path set).");
        }

    if (f3_wanted(g_home)) { tui_done(); if (f3_enter()) g3 = 1; else tui_init(); }   /* the 3D menu is on */
    for (;;)
    {
        if (g3)
        {
            int i;
            g3_build(&g3_v, row, col);
            f3_frame(&g3_v);
            c = f3_key(&g3_v);
            if (!c) continue;
            if (g3_row[g3_v.sel] >= 0) row = g3_row[g3_v.sel];            /* clicked a row */
            if (c == F3K_REDRAW || c == F3K_TABSEL) continue;
            if (c == F3K_F8) { f3_cfg_off(); f3_leave(); g3 = 0; tui_init(); continue; }
            if (c == K_ENTER && row < 8) c = ' ';                              /* Enter / a click switches games and values */
            if (c == K_UP || c == K_DOWN)
            {
                g3_v.sel = f3_step(&g3_v, c == K_UP ? -1 : 1);
                for (i = 0; i < g3_v.nrows; i++) if (i == g3_v.sel) row = g3_row[i];
                continue;
            }
        }
        else
        {
            draw_menu(row, col);
            c = tui_key();
            if (c == 0x142)                                                     /* F8: 3D on */
            {
                f3_cfg_load(g_home); f3_cfg_save(); tui_done();
                if (f3_enter()) g3 = 1;
                else { f3_cfg_off(); tui_init(); tui_message("3D screens", "This PC's video BIOS has no 640x480 VESA mode with 15/16/24/32-bit colour."); }
                continue;
            }
        }
        if (c == K_ESC) break;
        if (c == 0x13E) { tui_pick_mode(g_home); continue; }                  /* F4: display mode */
        if (c == K_F3) { tui_theme_next(g_home); continue; }                  /* F3: next style */
        if (c == K_UP) { row = row ? row - 1 : ROWS - 1; continue; }
        if (c == K_DOWN) { row = row < ROWS - 1 ? row + 1 : 0; continue; }
        if (c == K_LEFT || c == K_RIGHT)
        {
            int dx = c == K_LEFT ? -1 : 1;
            if (row >= 4 && row < 8) { k = row - 4; col[k] = (col[k] + dx + g_opts[k].nval) % g_opts[k].nval; }
            if (row == 8) { g_secs += dx * 5; if (g_secs < 20) g_secs = 20; if (g_secs > 255) g_secs = 255; save_cfg(); }
            continue;
        }
        if (row < 4)
        {
            GAME* g = &g_games[row];
            int u = c < 256 ? toupper(c) : 0;
            if (c == ' ') g->on = !g->on;
            if (u == 'E') { char t[80]; strcpy(t, g->path); if (ui_input(g->name, "Path of the game's .EXE:", t, 79, 1)) strcpy(g->path, t); }
            if (u == 'A') { char t[64]; strcpy(t, g->args); if (ui_input(g->name, "Command-line options for the test:", t, 63, 0)) strcpy(g->args, t); }
            if (u == 'S' || u == 'Q')
            {
                char t[64], bad[16]; unsigned char tmp[96]; char* dst = u == 'S' ? g->kstart : g->kquit;
                strcpy(t, dst);
                if (ui_input(g->name, u == 'S' ? "Keys typed when the game starts (W2 = wait 2 s, ESC ENTER F10 Y ...):"
                                                : "Keys typed when the test time is up (to quit the game):", t, 63, 1))
                {
                    if (compile_keys(t, tmp, 40, bad) < 0)
                    { char m[80]; sprintf(m, "Unknown key '%s'. Use ESC ENTER SPACE F1-F12 UP A-Z 0-9 W2.", bad); ui_message("Keys", m); }
                    else strcpy(dst, t);
                }
            }
            save_cfg();
        }
        else if (row < 8 && c == ' ')
        {
            k = row - 4;
            g_opts[k].sel ^= 1u << col[k];
            if (!g_opts[k].sel) g_opts[k].sel = 1u << col[k]; /* at least one value */
            save_cfg();
        }
        else if (c == K_ENTER && (row == 9 || row == 10))
        {
            char buf[120];
            int n = build_plan(row == 10);
            save_cfg();
            if (!n) { ui_message("GAMETEST", "No game selected (or no path set)."); continue; }
            if (!sbemu_present()) { ui_message("GAMETEST", "SBEMU's MPU-401 does not answer: run SC55 first."); continue; }
            sprintf(buf, "Run %d tests (about %d min)? Keep your hands off the keyboard.", n, (n * (g_secs + 20) + 59) / 60);
            if (ui_yesno(buf)) { g3_text_begin(); run_plan(0, n); g3_text_end(); }
        }
        else if (c == K_ENTER && row == 11) { g3_text_begin(); show_file("GAMETEST.TXT"); g3_text_end(); }
        else if (c == K_ENTER && row == 12) { g3_text_begin(); show_file("GTFULL.TXT"); g3_text_end(); }
        else if (c == K_ENTER && row == 13) { g3_text_begin(); show_best(); g3_text_end(); }
        else if (c == K_ENTER && row == 14) clear_results();
    }
    save_cfg();
    if (g3) f3_leave();
    tui_done();
    return 0;
}
