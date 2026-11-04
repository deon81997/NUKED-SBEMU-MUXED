/* SC55MENU - one screen for everything in NUKED SB-EMU (the SC-55 pack). Started by C:\SC55\MENU.BAT.
 *
 * The menu never starts SBEMU (or a game) itself: it writes the chosen command to MENUGO.BAT and
 * exits, and MENU.BAT runs that file from plain DOS. SBEMU stays resident, so it must not be loaded
 * from inside another program. After most choices MENU.BAT brings the menu back (MENUBACK=1);
 * Esc just exits. MENUVIEW=<file> in MENUGO.BAT makes the menu show that file on return.
 *
 * Games are played with the SBEMU settings that tested best for them in GAMETEST (GTBEST.CFG), if
 * there are any: the resident SBEMU takes them over before the game and goes back to your saved
 * settings afterwards. F2 plays with your saved settings instead.
 *
 *   SC55MENU [home folder]     (default: the folder SC55MENU.EXE is in)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <dir.h>
#include "tui.h"
#include "safewr.h"

#define MPU 0x330
#define NGAMES 4

static char g_home[200];
static int  g_running;          /* SBEMU answers at port 330 */
static char g_opts[300];        /* saved settings (SBOPTS.TXT / SBOPTS.BAT) */
static char g_optfile[16];      /* "@SBOPTS.TXT" or "@SBOPTS.BAT" or "" */
static int  g_gt_green, g_gt_yellow, g_gt_red, g_gt_lock;

enum { IT_BAT, IT_VIEW, IT_GAME, IT_EDIT };
enum { C_SOUND, C_SETTINGS, C_TESTS, C_GAMES, C_CD, C_FILES, NCAT };
static const char* g_catname[NCAT] = { "Sound", "Settings", "Tests", "Games", "CD-ROM", "Files" };
static const unsigned char g_caticon[NCAT] = { 0x0E, 0x0F, 0xFB, 0x02, 0x0A, 0xF0 };
typedef struct {
    int cat;                    /* category (tab on the left) */
    char label[40];
    char desc[400];
    int kind;
    char cmd[300];              /* IT_BAT: lines for MENUGO.BAT ('|' = new line); IT_VIEW: file name */
    int needs_sbemu;            /* 1: only when SBEMU is loaded, -1: only when it is not */
    int pause;                  /* PAUSE after the command */
    char view[16];              /* file shown when the menu comes back */
    char best[120];             /* IT_GAME: SBEMU options that tested best ("" = none) */
    char cmd2[300];             /* IT_GAME: the same game with the saved settings (F2) */
    int gp;                     /* IT_GAME: game profile number */
} ITEM;
static ITEM g_it[80];
static int g_n;

/* ---------------------------------------------------------------- helpers */
static int sbemu_present(void)
{
    long i;
    outportb(MPU + 1, 0xFF);
    for (i = 0; i < 200000L; ++i)
        if (!(inportb(MPU + 1) & 0x80)) { if (inportb(MPU) == 0xFE) { outportb(MPU + 1, 0x3F); return 1; } break; }
    return 0;
}
/* SC-55 / SoundFont render load from SBEMU (MPU command 0xB0, beta.34+), permille; -1 = not known */
static int g_load = -1;
static unsigned long g_load_tick;
static void sbemu_load(void)
{
    unsigned char b[150]; int i;
    unsigned long t = _farpeekl(_dos_ds, 0x46C);
    if (!g_running || (g_load_tick && t - g_load_tick < 18)) return;   /* once a second */
    g_load_tick = t ? t : 1;
    outportb(MPU + 1, 0xB0);
    for (i = 0; i < 150; i++) b[i] = (unsigned char)inportb(MPU);       /* the whole report, so nothing is left over */
    g_load = b[0] == 'L' ? b[3] | (b[4] << 8) : -1;
}
static void home_path(char* out, const char* name) { sprintf(out, "%s\\%s", g_home, name); }
static int home_exists(const char* name) { char p[300]; home_path(p, name); return access(p, R_OK) == 0; }

static void read_sbopts(void)
{
    char p[300], line[300]; FILE* f;
    g_opts[0] = 0; g_optfile[0] = 0;
    home_path(p, "SBOPTS.TXT");
    if ((f = fopen(p, "r")) != NULL)
    {
        if (fgets(line, sizeof line, f)) { line[strcspn(line, "\r\n")] = 0; strcpy(g_opts, line); }
        fclose(f); strcpy(g_optfile, "@SBOPTS.TXT");
        return;
    }
    home_path(p, "SBOPTS.BAT");
    if ((f = fopen(p, "r")) == NULL) return;
    while (fgets(line, sizeof line, f))
    {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncasecmp(line, "SET SBOPTS=", 11)) { strncpy(g_opts, line + 11, sizeof g_opts - 1); g_opts[sizeof g_opts - 1] = 0; }
    }
    fclose(f); strcpy(g_optfile, "@SBOPTS.BAT");
}
/* value of "/A:220" style option in the saved settings, or def */
static const char* opt_val(const char* name, const char* def)
{
    static char ring[4][80]; static int k;
    char* v = ring[k++ & 3];                    /* several values can be used in one printf */
    const char* p = g_opts; size_t n = strlen(name);
    while ((p = strstr(p, name)) != NULL)
    {
        if ((p == g_opts || p[-1] == ' ') && (p[n] == ':' || p[n] == '='))
        {
            int i = 0; p += n + 1;
            while (*p && *p != ' ' && i < 79) v[i++] = *p++;
            v[i] = 0; return v;
        }
        p += n;
    }
    return def;
}

/* clock classes: 0 green 99-101%, 1 yellow 95-99% or 101-105%, 2 red (too slow / too fast / locked up) */
static int clk_class(double v) { return (v >= 99.0 && v <= 101.0) ? 0 : ((v >= 95.0 && v <= 105.0) ? 1 : 2); }

static void read_gametest_summary(void)
{
    char p[300], line[400]; FILE* f;
    g_gt_green = g_gt_yellow = g_gt_red = g_gt_lock = 0;
    home_path(p, "GAMETEST.TXT");
    if ((f = fopen(p, "r")) == NULL) return;
    while (fgets(line, sizeof line, f))
    {
        const char* c = strstr(line, "clock ");
        double v;
        if (strstr(line, "LOCKED")) { ++g_gt_lock; continue; }
        if (c && sscanf(c + 6, " %lf%%", &v) == 1)
        { int k = clk_class(v); if (k == 0) ++g_gt_green; else if (k == 1) ++g_gt_yellow; else ++g_gt_red; }
    }
    fclose(f);
}

static ITEM* add(int cat, const char* label, const char* desc, int kind, const char* cmd, int needs, int pause, const char* view)
{
    ITEM* it;
    if (g_n >= 80) return NULL;
    it = &g_it[g_n++];
    memset(it, 0, sizeof *it);
    it->cat = cat; it->kind = kind; it->needs_sbemu = needs; it->pause = pause;
    snprintf(it->label, sizeof it->label, "%s", label);
    snprintf(it->desc, sizeof it->desc, "%s", desc);
    if (cmd) snprintf(it->cmd, sizeof it->cmd, "%s", cmd);
    if (view) snprintf(it->view, sizeof it->view, "%s", view);
    return it;
}

/* ---------------------------------------------------------------- game profiles (GAMES.CFG)
 * One line per game:  name|program|arguments|SBEMU options|CD image
 *   program        full path of the .EXE / .COM / .BAT
 *   SBEMU options  used while this game runs (on top of the saved settings), e.g. /VSCSET:mk1-v1.21 /K:22050
 *   CD image       a file in C:\CDIMAGES mounted on drive M: before the game starts ("" = none)
 * Made from GAMETEST's games the first time. GTBEST.CFG (GAMETEST): BEST<n>=<clock x10>|<options>; a
 * profile whose program is GAMETEST game n uses those options too (its own come after them and win). */
#define MAXGP 24
typedef struct { char name[32], exe[80], args[64], opts[120], cd[13]; } GAMEP;
static GAMEP g_gp[MAXGP];
static int g_ngp;
static char g_gtpath[NGAMES][80], g_best[NGAMES][120]; static int g_bestclk[NGAMES];

static void field(char* dst, size_t sz, char** p)
{
    char* e = strchr(*p, '|');
    size_t len = e ? (size_t)(e - *p) : strlen(*p), n = len < sz ? len : sz - 1;
    memcpy(dst, *p, n); dst[n] = 0;
    *p += len + (e ? 1 : 0);
}
static void gp_load(void)
{
    char p[300], line[512]; FILE* f; int i;
    static const char* names[NGAMES] = { "Duke Nukem 3D", "Doom", "Doom II", "" };
    char defpath[NGAMES][80] = { "C:\\DUKE3D\\DUKE3D.EXE", "C:\\DOOM\\DOOM.EXE", "C:\\DOOM2\\DOOM2.EXE", "" };
    memset(g_gtpath, 0, sizeof g_gtpath); memset(g_best, 0, sizeof g_best); memset(g_bestclk, 0, sizeof g_bestclk);
    for (i = 0; i < NGAMES; i++) snprintf(g_gtpath[i], sizeof g_gtpath[i], "%s", defpath[i]);
    home_path(p, "GAMETEST.CFG");
    if ((f = fopen(p, "r")) != NULL)
    {
        while (fgets(line, sizeof line, f))
        {
            char *a, *b;
            line[strcspn(line, "\r\n")] = 0;
            if (sscanf(line, "GAME%d=", &i) == 1 && i >= 0 && i < NGAMES && (a = strchr(line, '|')) != NULL && (b = strchr(a + 1, '|')) != NULL)
            { *b = 0; snprintf(g_gtpath[i], sizeof g_gtpath[i], "%s", a + 1); }
        }
        fclose(f);
    }
    home_path(p, "GTBEST.CFG");
    if ((f = fopen(p, "r")) != NULL)
    {
        while (fgets(line, sizeof line, f))
        {
            char* a; int clk;
            line[strcspn(line, "\r\n")] = 0;
            if (sscanf(line, "BEST%d=%d|", &i, &clk) == 2 && i >= 0 && i < NGAMES && (a = strchr(line, '|')) != NULL)
            { g_bestclk[i] = clk; snprintf(g_best[i], sizeof g_best[i], "%s", a + 1); }
        }
        fclose(f);
    }
    g_ngp = 0;
    home_path(p, "GAMES.CFG");
    if ((f = fopen(p, "r")) != NULL)
    {
        while (fgets(line, sizeof line, f) && g_ngp < MAXGP)
        {
            char* q = line; GAMEP* g = &g_gp[g_ngp];
            line[strcspn(line, "\r\n")] = 0;
            if (!line[0] || line[0] == ';' || !strchr(line, '|')) continue;
            memset(g, 0, sizeof *g);
            field(g->name, sizeof g->name, &q); field(g->exe, sizeof g->exe, &q); field(g->args, sizeof g->args, &q);
            field(g->opts, sizeof g->opts, &q); field(g->cd, sizeof g->cd, &q);
            if (g->exe[0]) g_ngp++;
        }
        fclose(f);
        return;
    }
    /* first time: GAMETEST's games that exist */
    for (i = 0; i < NGAMES && g_ngp < MAXGP; i++)
        if (g_gtpath[i][0] && access(g_gtpath[i], R_OK) == 0)
        {
            GAMEP* g = &g_gp[g_ngp++]; const char* bs = strrchr(g_gtpath[i], '\\');
            memset(g, 0, sizeof *g);
            snprintf(g->name, sizeof g->name, "%s", names[i][0] ? names[i] : (bs ? bs + 1 : g_gtpath[i]));
            snprintf(g->exe, sizeof g->exe, "%s", g_gtpath[i]);
        }
}
static int gp_save(void)
{
    char p[300], tmp[310]; FILE* f; int i;
    home_path(p, "GAMES.CFG");
    if (!(f = sw_open(p, tmp, "wb"))) return 0;
    fprintf(f, "; NUKED SB-EMU game list: name|program|arguments|SBEMU options|CD image (edit in MENU > Games)\r\n");
    for (i = 0; i < g_ngp; i++)
        fprintf(f, "%s|%s|%s|%s|%s\r\n", g_gp[i].name, g_gp[i].exe, g_gp[i].args, g_gp[i].opts, g_gp[i].cd);
    return sw_close(f, tmp, p);
}
static int gp_test_slot(const GAMEP* g)
{
    int i;
    for (i = 0; i < NGAMES; i++) if (g_gtpath[i][0] && !strcasecmp(g_gtpath[i], g->exe)) return i;
    return -1;
}

static void add_games(void)
{
    int i;
    gp_load();
    for (i = 0; i < g_ngp; ++i)
    {
        const GAMEP* g = &g_gp[i];
        char lab[40], desc[400], cmd[300], dir[80], exe[80], extra[200], *bs;
        int slot = gp_test_slot(g), have = access(g->exe, R_OK) == 0, isbat;
        ITEM* it;
        snprintf(dir, sizeof dir, "%s", g->exe);
        bs = strrchr(dir, '\\'); if (!bs || dir[1] != ':') continue;
        snprintf(exe, sizeof exe, "%s", bs + 1); *bs = 0;
        if (strlen(dir) == 2) strcat(dir, "\\");
        isbat = strlen(exe) > 4 && !strcasecmp(exe + strlen(exe) - 4, ".BAT");
        snprintf(lab, sizeof lab, "Play %.30s", g->name);
        snprintf(cmd, sizeof cmd, "%c:|CD %s|%s%s%s%s", toupper((unsigned char)dir[0]), dir + 2, isbat ? "CALL " : "", exe,
                 g->args[0] ? " " : "", g->args);
        extra[0] = 0;
        if (g->cd[0]) snprintf(extra + strlen(extra), sizeof extra - strlen(extra), " Mounts %s on drive M: first.", g->cd);
        if (g->opts[0]) snprintf(extra + strlen(extra), sizeof extra - strlen(extra), " Own SBEMU options: %.100s.", g->opts);
        if (!have)
            snprintf(desc, sizeof desc, "%s is not there (moved or on another drive?). Change it in \"Add / edit games\".", g->exe);
        else if (slot >= 0 && g_best[slot][0])
            snprintf(desc, sizeof desc, "Starts %s%s%s with the SBEMU settings that gave it the best music timing in GAMETEST"
                     " (music clock %d.%d%%): %s.%s SBEMU goes back to your saved settings afterwards. F2: your saved settings only.",
                     g->exe, g->args[0] ? " " : "", g->args, g_bestclk[slot] / 10, g_bestclk[slot] % 10, g_best[slot], extra);
        else
            snprintf(desc, sizeof desc, "Starts %s%s%s and brings you back here when you quit it.%s Start the SC-55 first for"
                     " General MIDI music.", g->exe, g->args[0] ? " " : "", g->args, extra);
        it = add(C_GAMES, lab, desc, IT_GAME, cmd, 0, 0, NULL);
        if (it)
        {
            snprintf(it->cmd2, sizeof it->cmd2, "%s", cmd);
            if (slot >= 0) snprintf(it->best, sizeof it->best, "%s", g_best[slot]);
            it->gp = i;
            if (!have) it->needs_sbemu = 2;            /* greyed out */
        }
    }
    add(C_GAMES, "Add / edit games", "Your list of games: name, program, arguments, SBEMU options just for that game (for example"
        " another ROM set or sample rate) and a CD image to mount before it starts. Saved in GAMES.CFG.", IT_EDIT, "", 0, 0, NULL);
}

static void build_menu(void)
{
    char c[300];
    g_n = 0;
    /* Sound */
    snprintf(c, sizeof c, "CALL %s\\SC55.BAT", g_home);
    add(C_SOUND, "Start SC-55 / SoundFont", "Loads SBEMU with your saved settings: a Sound Blaster plus an MPU-401 at port 330 played by the"
        " SC-55 emulation (or the SoundFont chosen in Settings). For protected-mode games (DOS4GW and the like): Duke 3D, Doom,"
        " most 90s games. Shows SBEMU's messages, then comes back here.", IT_BAT, c, -1, 1, NULL);
    if (home_exists("SC55JEM.BAT"))
    { snprintf(c, sizeof c, "CALL %s\\SC55JEM.BAT", g_home);
      add(C_SOUND, "Start for real-mode (JEMM)", "The same, but also for real-mode games (older games that do not use DOS4GW)."
          " Needs JEMMEX loaded from CONFIG.SYS - JEMMCFG.BAT sets that up, RESET.BAT undoes it.", IT_BAT, c, -1, 1, NULL); }
    if (home_exists("SC55QMM.BAT"))
    { snprintf(c, sizeof c, "CALL %s\\SC55QMM.BAT", g_home);
      add(C_SOUND, "Start for real-mode (QEMM)", "The same, for real-mode games too, with QEMM instead of JEMM. Needs QEMM386.SYS"
          " loaded from CONFIG.SYS (your own copy of QEMM).", IT_BAT, c, -1, 1, NULL); }
    /* Settings */
    snprintf(c, sizeof c, "CALL %s\\SETTINGS.BAT", g_home);
    add(C_SETTINGS, "Sound Blaster & MIDI", "The main settings screen: Sound Blaster port, IRQ, DMA and type, the MPU-401 port, the MIDI"
        " synth (SC-55, MT-32, CM-32L or a SoundFont), and the sample rate. Saved to SBOPTS.TXT. If SBEMU is already loaded, restart the PC so the new"
        " settings are used.", IT_BAT, c, 0, 0, NULL);
    snprintf(c, sizeof c, "CALL %s\\SETTINGS.BAT /TUNE", g_home);
    add(C_SETTINGS, "Automatic setup", "Measures how much of this PC's CPU the SC-55 needs - each ROM set you have, with and"
        " without reverb/chorus - and picks the best-sounding settings that leave enough for the game: ROM set, reverb/chorus,"
        " sample rate and note limit. About a minute; nothing is played. You see the numbers and choose whether to save them."
        " Only while SBEMU is not loaded (restart the PC first).", IT_BAT, c, -1, 0, NULL);
    snprintf(c, sizeof c, "CALL %s\\SETTINGS.BAT /PAGE:SC", g_home);
    add(C_SETTINGS, "SC-55 options", "SC-55 emulation: which ROM set (auto, SC-55, SC-55mkII), its volume (/VSCGAIN),"
        " reverb and chorus on or off (/VSCFX), a note limit (/VSCVOICES) and the automatic speed control (/VSCAUTO).",
        IT_BAT, c, 0, 0, NULL);
    snprintf(c, sizeof c, "CALL %s\\SETTINGS.BAT /PAGE:SF", g_home);
    add(C_SETTINGS, "SoundFont options", "Use a SoundFont (.sf2) instead of the SC-55: the file, how many voices, reverb level,"
        " room size and damping, chorus level, speed and depth, and whether the song's own reverb/chorus amounts are used."
        " Much lighter on the CPU than the SC-55.", IT_BAT, c, 0, 0, NULL);
    snprintf(c, sizeof c, "CALL %s\\SETTINGS.BAT /PAGE:VOL", g_home);
    add(C_SETTINGS, "Volume", "Master volume (/VOL), sound effects (/VSBVOL), FM music (/VOPLVOL) and General MIDI music"
        " (/VMIDIVOL), plus the SC-55 gain and the CD-audio volume. All on top of each game's own volume settings.",
        IT_BAT, c, 0, 0, NULL);
    snprintf(c, sizeof c, "CALL %s\\SETTINGS.BAT /PAGE:ADV", g_home);
    add(C_SETTINGS, "Timing & advanced", "Render-ahead and timer give-back (these keep game music at the right tempo), OPL on/off"
        " and core, output jack, sample-rate fix, CD audio, protected/real-mode support, which sound card, MIDI out port.",
        IT_BAT, c, 0, 0, NULL);
    /* Tests */
    add(C_TESTS, "MIDI player (MIDPLAY)", "Browse your folders for .MID files and play them through the running SC-55 or"
        " SoundFont. A quick way to hear the synth without a game.", IT_BAT, "MIDPLAY.EXE /B", 1, 0, NULL);
    add(C_TESTS, "Test tune (MPUTEST)", "Plays a short tune on the MPU-401 at port 330. If you hear it, General MIDI works.",
        IT_BAT, "MPUTEST.EXE", 1, 1, NULL);
    snprintf(c, sizeof c, "CALL %s\\TEST.BAT", g_home);
    add(C_TESTS, "ROM + speed test", "Checks that your SC-55 ROMs boot and measures how fast this PC runs the emulation"
        " (written to SC55LOG.TXT). Under 1.5x real time is too slow for the SC-55.", IT_BAT, c, 0, 1, NULL);
    add(C_TESTS, "Game test (GAMETEST)", "Runs Duke 3D, Doom and Doom II with each SBEMU option you pick and measures the"
        " music timing. Unattended: the games quit by themselves. Remembers the best settings for each game, which Play uses.",
        IT_BAT, "GAMETEST.EXE", 1, 0, NULL);
    add(C_TESTS, "SBDIAG: start measuring", "Starts SBEMU's self-measurement. Then play a game for a minute, quit it, and pick"
        " 'SBDIAG: report' to see why music would play too slow.", IT_BAT, "SBDIAG.EXE START", 1, 1, NULL);
    add(C_TESTS, "SBDIAG: report", "Reads the last measurement: lost timer ticks, how long SBEMU's sound interrupt takes,"
        " synth load, and what would fix it. Also saved to SBDIAG.TXT.", IT_BAT, "SBDIAG.EXE > NUL", 1, 0, "SBDIAG.TXT");
    /* Games */
    add_games();
    /* CD-ROM */
    if (home_exists("CDROM.BAT"))
    { snprintf(c, sizeof c, "CALL %s\\CDROM.BAT", g_home);
      add(C_CD, "CD-ROM drive letter", "Gives the real CD-ROM drive a letter (D:) with MSCDEX. The CD-ROM driver must already"
          " be loaded from CONFIG.SYS (BOOTCFG.BAT does that) - see DOSMODE\\CDROM-D810.TXT.", IT_BAT, c, 0, 1, NULL); }
    if (access("C:\\CDSET\\GUI.BAT", R_OK) == 0)
    { snprintf(c, sizeof c, "CALL C:\\CDSET\\GUI.BAT %s", g_home);
      add(C_CD, "CD images", "Mount .CUE/.BIN, .ISO or .MDM disc images from C:\\CDIMAGES as drive M:. Shows each image's"
          " tracks and size, fixes CUE files that name a long BIN file name DOS cannot open (F), and tells you if the"
          " mount worked. CD-audio tracks play through SBEMU - start games with HYBRID.BAT for that.", IT_BAT, c, 0, 0, NULL); }
    /* Files */
    add(C_FILES, "SBEMU messages (SBLOG)", "What SBEMU printed when it was last started: sound card, sample rate, SC-55 ROM"
        " set, errors.", IT_VIEW, "SBLOG.TXT", 0, 0, NULL);
    add(C_FILES, "Last SBDIAG report", "SBDIAG.TXT: the reasons game music would play too slow, most important first.",
        IT_VIEW, "SBDIAG.TXT", 0, 0, NULL);
    add(C_FILES, "GAMETEST results", "GAMETEST.TXT: one line per test. Green = right tempo, yellow = a bit off, red = slow,"
        " too fast or locked up. Right arrow shows the rest of each line.", IT_VIEW, "GAMETEST.TXT", 0, 0, NULL);
    add(C_FILES, "GAMETEST full reports", "GTFULL.TXT: the complete SBDIAG report of every GAMETEST test.",
        IT_VIEW, "GTFULL.TXT", 0, 0, NULL);
    add(C_FILES, "README", "The pack's manual: installing, every option, the tools, troubleshooting.",
        IT_VIEW, "README.TXT", 0, 0, NULL);
}

static int usable(const ITEM* it)
{
    if (it->needs_sbemu == 2) return 0;
    if (it->needs_sbemu > 0 && !g_running) return 0;
    if (it->needs_sbemu < 0 && g_running) return 0;
    return 1;
}

/* items of one category, in order */
static int cat_items(int cat, int* out)
{
    int i, n = 0;
    for (i = 0; i < g_n; i++) if (g_it[i].cat == cat) out[n++] = i;
    return n;
}

/* ---------------------------------------------------------------- drawing */
#define BG_DESK   1     /* deep blue */
static int g_phase;
static const unsigned char g_rainbow[] = { 9, 11, 10, 14, 12, 13 };

static void draw_header(void)
{
    int x;
    tui_fill(1, 1, 80, 5, BG_DESK << 4, ' ');
    if (TUI_FANCY)
    {
        x = 2 + tui_biglogo(2, 1, BG_DESK, "NUKED", g_rainbow, sizeof g_rainbow, g_phase);
        tui_biglogo(x + 2, 1, BG_DESK, "SB-EMU", g_rainbow, sizeof g_rainbow, g_phase + 5);
    }
    else
    {
        static const unsigned char plain[] = { 15 };
        x = 2 + tui_biglogo(2, 1, BG_DESK, "NUKED", plain, 1, 0);
        tui_biglogo(x + 2, 1, BG_DESK, "SB-EMU", (const unsigned char*)"\x0B", 1, 0);
    }
    tui_put(3, 5, (BG_DESK << 4) | 0x0B, "SBEMU \xFA Nuked-SC55 \xFA MT-32 \xFA SoundFont \xFA for DOS");
}

static void draw_status(void)
{
    const char* sf = opt_val("/VSF", "");
    int a = (BG_DESK << 4);
    /* narrower than the other screens' panels: the NUKED SB-EMU logo needs the room */
    tui_panel(59, 1, 21, 5, 0, 0x0E, "Status");
    if (g_running)
    {
        tui_put(61, 2, 0x0A, "\xFE SBEMU on");
        sbemu_load();
        if (g_load >= 0)
            tui_printf(71, 2, g_load < 500 ? 0x0A : g_load < 800 ? 0x0E : 0x0C, "CPU%3d%%", (g_load + 5) / 10 > 999 ? 999 : (g_load + 5) / 10);
    }
    else tui_put(61, 2, 0x0C, "\xFE SBEMU off");
    const char* mt = opt_val("/VMT32", "");
    if (mt[0] && strcmp(mt, "0") && strcasecmp(mt, "OFF")) tui_printf(61, 3, 0x07, "%s (Munt)", !strcasecmp(mt, "CM32L") ? "CM-32L" : "MT-32");
    else if (sf[0]) { const char* b = strrchr(sf, '\\'); tui_printf(61, 3, 0x07, "SF %.14s", b ? b + 1 : sf); }
    else tui_printf(61, 3, 0x07, "SC-55 %.11s", opt_val("/VSCSET", "auto"));
    tui_printf(61, 4, 0x07, "SB %s I%s D%s", opt_val("/A", "220"), opt_val("/I", "7"), opt_val("/D", "1"));
    (void)a;
}

static int g_itop;                    /* first item shown (the list scrolls past 6 items) */
static int g_reveal = 99;             /* items shown so far after a category change (animation) */
static void draw(int cat, int sel)
{
    int idx[80], n = cat_items(cat, idx), i, y;
    if (n > g_reveal) n = g_reveal;
    const ITEM* it = &g_it[sel];
    tui_clear(BG_DESK << 4);
    draw_header();
    draw_status();

    /* categories */
    tui_panel(2, 7, 17, 9, 3, 0x3F, "Menu");
    for (i = 0; i < NCAT; i++)
    {
        int k[80], cnt = cat_items(i, k), a;
        char b[20];
        if (!cnt) a = 0x38;
        else if (i == cat) a = TUI_FANCY ? 0x5F : 0x1F;
        else a = 0x30;
        snprintf(b, sizeof b, " %c %-10s", g_caticon[i], g_catname[i]);
        tui_putw(3, 9 + i, 15, a, b);
        if (i == cat) { tui_cell(17, 9 + i, a, 0x10); }
    }

    /* items of the chosen category */
    {
        char t[48];
        snprintf(t, sizeof t, "%c %s", g_caticon[cat], g_catname[cat]);
        tui_panel(20, 7, 60, 9, 7, 0x1F, t);
    }
    if (!n) tui_put(23, 9, 0x78, "(nothing here - see the description)");
    {
        int pos = 0, top;
        for (i = 0; i < n; i++) if (idx[i] == sel) pos = i;
        top = pos >= 6 ? pos - 5 : 0;
        g_itop = top;
        if (top) { memmove(idx, idx + top, (size_t)(n - top) * sizeof idx[0]); n -= top; tui_cell(78, 8, 0x78, 0x18); }
        if (n > 6) tui_cell(78, 15, 0x78, 0x19);
    }
    for (i = 0, y = 9; i < n && i < 6; i++, y++)
    {
        const ITEM* t = &g_it[idx[i]];
        int on = usable(t), a;
        char b[64];
        if (idx[i] == sel) a = TUI_FANCY ? (on ? 0x5F : 0x57) : (on ? 0x1F : 0x17);
        else a = on ? 0x70 : 0x78;
        if (g_itop + i < 9) snprintf(b, sizeof b, " %d  %s", g_itop + i + 1, t->label);
        else snprintf(b, sizeof b, "    %s", t->label);
        tui_putw(22, y, 56, a, b);
        if (t->kind == IT_GAME && t->best[0]) tui_cell(76, y, idx[i] == sel ? (a & 0xF0) | 0x0E : 0x72, 0x04);
        if (idx[i] == sel) tui_cell(21, y, 0x7D, 0x10);
    }

    /* full description of the highlighted item */
    tui_panel(2, 16, 78, 8, 0, 0x0E, it->label);
    {
        int used = tui_wrap(4, 17, 74, 5, 0x07, it->desc);
        if (!usable(it))
            tui_putw(4, 17 + used, 74, 0x0C, it->needs_sbemu == 2 ? "\xFE Program not found - fix it in Games > Add / edit games."
                                                                  : it->needs_sbemu > 0 ? "\xFE Start the SC-55 first (Sound > Start SC-55 / SoundFont)."
                                                                  : "\xFE SBEMU is already loaded. Restart the PC to load it again.");
    }

    /* bottom: GAMETEST summary and keys */
    tui_fill(1, 24, 80, 1, BG_DESK << 4, ' ');
    {
        int x = 3;
        if (g_gt_green + g_gt_yellow + g_gt_red + g_gt_lock)
        {
            x += tui_put(x, 24, (BG_DESK << 4) | 0x07, "GAMETEST: ");
            x += tui_printf(x, 24, (BG_DESK << 4) | 0x0A, "%d right  ", g_gt_green);
            x += tui_printf(x, 24, (BG_DESK << 4) | 0x0E, "%d a bit off  ", g_gt_yellow);
            x += tui_printf(x, 24, (BG_DESK << 4) | 0x0C, "%d slow/fast/locked up", g_gt_red + g_gt_lock);
        }
        else tui_put(x, 24, (BG_DESK << 4) | 0x08, "No GAMETEST results yet.");
        tui_printf(60, 24, (BG_DESK << 4) | 0x0B, "%s \xFA %dx%d", tui_theme_name[tui_theme], tui_cols, tui_rows);
    }
    tui_fill(1, 25, 80, 1, 0x30, ' ');
    tui_put(2, 25, 0x30, "\x18\x19\x1B\x1A Enter F1 help F2 saved F3 style F4 screen F5 MIDI F6 anim F7 font Esc");
}

static int g_cat, g_sel;

/* ---------------------------------------------------------------- game list editor (Games > Add / edit games) */
typedef struct { char name[13]; int dir; } PENT;
static int pent_cmp(const void* a, const void* b)
{
    const PENT *x = (const PENT*)a, *y = (const PENT*)b;
    if (x->dir != y->dir) return y->dir - x->dir;
    return strcmp(x->name, y->name);
}
/* picks a program (.EXE .COM .BAT) with a folder browser; T types the path. Returns 1 with out set. */
static int pick_program(char* out, size_t sz)
{
    static PENT e[400];
    char dir[160], pat[200];
    int n, sel = 0, top = 0, i, c;
    uint16_t save[TUI_H][TUI_W];
    snprintf(dir, sizeof dir, "%s", out[0] && out[1] == ':' ? out : "C:\\");
    { char* bs = strrchr(dir, '\\'); if (bs && out[0]) bs[1] = 0; }
    if (access(dir, D_OK) != 0) snprintf(dir, sizeof dir, "C:\\");
    memcpy(save, tui_scr, sizeof save);
    tui_zoom(10, 3, 60, 21, 0x1F);
    for (;;)
    {
        struct ffblk ff; int r;
        n = 0;
        if (strlen(dir) > 3) { strcpy(e[n].name, ".."); e[n++].dir = 2; }
        snprintf(pat, sizeof pat, "%s*.*", dir);
        for (r = findfirst(pat, &ff, FA_DIREC | FA_RDONLY | FA_ARCH); r == 0 && n < 400; r = findnext(&ff))
        {
            const char* x = strrchr(ff.ff_name, '.');
            int isdir = (ff.ff_attrib & FA_DIREC) != 0;
            if (ff.ff_name[0] == '.') continue;
            if (!isdir && !(x && (!strcasecmp(x, ".EXE") || !strcasecmp(x, ".COM") || !strcasecmp(x, ".BAT")))) continue;
            snprintf(e[n].name, sizeof e[n].name, "%s", ff.ff_name); e[n++].dir = isdir;
        }
        qsort(e, (size_t)n, sizeof e[0], pent_cmp);
        if (sel >= n) sel = n ? n - 1 : 0;
        for (;;)
        {
            char b[80];
            if (sel < top) top = sel;
            if (sel >= top + 15) top = sel - 14;
            tui_window(10, 3, 60, 21, 0x1F, "Choose the game's program");
            tui_putw(12, 4, 56, 0x1E, dir);
            for (i = 0; i < 15; i++)
            {
                int k = top + i;
                if (k >= n) { tui_putw(12, 6 + i, 56, 0x1F, ""); continue; }
                snprintf(b, sizeof b, " %s%s", e[k].name, e[k].dir == 1 ? "\\" : e[k].dir == 2 ? "  (up)" : "");
                tui_putw(12, 6 + i, 56, k == sel ? 0x5F : (e[k].dir ? 0x1B : 0x1F), b);
            }
            if (!n) tui_put(13, 6, 0x17, "(no programs or folders here)");
            tui_put(12, 22, 0x13, "Enter open/choose  Bksp up  Tab drive  T type path  Esc");
            c = tui_key();
            if (c == K_ESC) { memcpy(tui_scr, save, sizeof save); return 0; }
            if (c == K_UP && sel > 0) sel--;
            else if (c == K_DOWN && sel + 1 < n) sel++;
            else if (c == K_PGUP) { sel -= 15; if (sel < 0) sel = 0; }
            else if (c == K_PGDN) { sel += 15; if (sel >= n) sel = n ? n - 1 : 0; }
            else if (c == 't' || c == 'T')
            {
                char t[80]; snprintf(t, sizeof t, "%s", out);
                if (tui_input("Program", "Full path of the program, e.g. C:\\DUKE3D\\DUKE3D.EXE", t, sizeof t, 1) && t[0])
                { snprintf(out, sz, "%s", t); memcpy(tui_scr, save, sizeof save); return 1; }
            }
            else if (c == 8 && strlen(dir) > 3) { dir[strlen(dir) - 1] = 0; *(strrchr(dir, '\\') + 1) = 0; sel = top = 0; break; }
            else if (c == K_TAB)
            {   /* next drive that has a disc (C: and up, floppies skipped) */
                int d = toupper((unsigned char)dir[0]), k;
                for (k = 1; k <= 24; k++)
                {
                    char nd[8]; int dl = 'C' + (d - 'C' + k) % 24;
                    snprintf(nd, sizeof nd, "%c:\\", dl);
                    if (access(nd, D_OK) == 0) { snprintf(dir, sizeof dir, "%s", nd); break; }
                }
                sel = top = 0; break;
            }
            else if (c == K_ENTER && n)
            {
                if (e[sel].dir == 2) { dir[strlen(dir) - 1] = 0; *(strrchr(dir, '\\') + 1) = 0; sel = top = 0; break; }
                if (e[sel].dir == 1) { strncat(dir, e[sel].name, sizeof dir - strlen(dir) - 2); strcat(dir, "\\"); sel = top = 0; break; }
                snprintf(out, sz, "%s%s", dir, e[sel].name);
                memcpy(tui_scr, save, sizeof save);
                return 1;
            }
        }
    }
}
/* picks a disc image in C:\CDIMAGES (or none) */
static void pick_cd(char* out)
{
    static char f[100][13];
    struct ffblk ff; int n = 1, r, sel = 0, top = 0, i, c, t;
    static const char* pats[] = { "C:\\CDIMAGES\\*.CUE", "C:\\CDIMAGES\\*.ISO", "C:\\CDIMAGES\\*.MDM" };
    uint16_t save[TUI_H][TUI_W];
    strcpy(f[0], "");
    for (t = 0; t < 3; t++)
        for (r = findfirst(pats[t], &ff, FA_RDONLY | FA_ARCH); r == 0 && n < 100; r = findnext(&ff))
            snprintf(f[n++], 13, "%s", ff.ff_name);
    for (i = 0; i < n; i++) if (!strcasecmp(f[i], out)) sel = i;
    memcpy(save, tui_scr, sizeof save);
    tui_zoom(24, 4, 32, 18, 0x1F);
    for (;;)
    {
        if (sel < top) top = sel;
        if (sel >= top + 13) top = sel - 12;
        tui_window(24, 4, 32, 18, 0x1F, "CD image");
        for (i = 0; i < 13; i++)
        {
            int k = top + i; char b[40];
            if (k < n) snprintf(b, sizeof b, " %s", k ? f[k] : "(no CD image)"); else b[0] = 0;
            tui_putw(26, 6 + i, 28, k == sel ? 0x5F : 0x1F, b);
        }
        tui_put(26, 20, 0x13, n > 1 ? "Enter choose   Esc back" : "No images in C:\\CDIMAGES");
        c = tui_key();
        if (c == K_ESC) break;
        if (c == K_UP && sel > 0) sel--;
        if (c == K_DOWN && sel + 1 < n) sel++;
        if (c == K_ENTER) { strcpy(out, f[sel]); break; }
    }
    memcpy(tui_scr, save, sizeof save);
}

static void draw_header(void);
static void draw_status(void);
static void game_editor(void)
{
    static const char* lab[5] = { "Name", "Program", "Arguments", "SBEMU options", "CD image" };
    static const char* hint[5] = {
        "What the menu calls it: \"Play <name>\".",
        "The game's .EXE, .COM or .BAT. Enter opens a folder browser (T in it types the path).",
        "Added after the program, e.g. -nosound or /setup. Leave empty for none.",
        "Used only while this game runs, on top of your saved settings, e.g. /VSCSET:mk1-v1.21 /K:22050 /VSCVOICES:16."
        " Needs SBEMU loaded. Empty = your saved settings (plus GAMETEST's best, if any).",
        "A disc image from C:\\CDIMAGES mounted on drive M: before the game starts (games that check for their CD,"
        " or have CD music). Enter picks one." };
    int sel = 0, fld = -1, c, i, dirty = 0;
    for (;;)
    {
        GAMEP* g = sel < g_ngp ? &g_gp[sel] : NULL;
        tui_clear(1 << 4);
        draw_header(); draw_status();
        tui_panel(2, 7, 28, 17, 3, 0x3F, "Games");
        for (i = 0; i <= g_ngp && i < 14; i++)
        {
            char b[32];
            int on = i == sel, a = on ? (fld < 0 ? (TUI_FANCY ? 0x5F : 0x1F) : 0x1B) : 0x30;
            if (i < g_ngp) snprintf(b, sizeof b, " %.24s", g_gp[i].name); else snprintf(b, sizeof b, " + new game");
            tui_putw(3, 9 + i, 26, i < g_ngp ? a : (on ? a : 0x3E), b);
        }
        tui_panel(31, 7, 49, 17, 7, 0x1F, g ? g->name : "New game");
        if (g)
        {
            const char* val[5]; char cdb[20];
            snprintf(cdb, sizeof cdb, "%s", g->cd[0] ? g->cd : "(none)");
            val[0] = g->name; val[1] = g->exe; val[2] = g->args; val[3] = g->opts; val[4] = cdb;
            for (i = 0; i < 5; i++)
            {
                int on = i == fld;
                tui_putw(33, 9 + i * 2, 45, on ? 0x70 : 0x78, lab[i]);
                tui_putw(33, 10 + i * 2, 45, on ? (TUI_FANCY ? 0x5F : 0x1F) : 0x70, val[i][0] ? val[i] : "-");
            }
            if (access(g->exe, R_OK) != 0) tui_put(33, 19, 0x7C, "\xFE The program is not there.");
            tui_wrap(33, 20, 45, 3, 0x71, fld >= 0 ? hint[fld] : "Enter: edit this game   Del: remove it");
        }
        else tui_wrap(33, 9, 45, 4, 0x70, "Press Enter to add a game: you pick its program first, then give it a name.");
        tui_fill(1, 25, 80, 1, 0x30, ' ');
        tui_put(2, 25, 0x30, fld < 0 ? "\x18\x19 choose  Enter edit / add  Del remove  Esc save and back"
                                     : "\x18\x19 field  Enter change it  Esc back to the list");
        c = tui_key();
        if (fld < 0)
        {
            if (c == K_ESC) break;
            if (c == K_UP && sel > 0) sel--;
            else if (c == K_DOWN && sel < g_ngp && sel < 13) sel++;
            else if ((c == 0x153 || c == 'd' || c == 'D') && g)
            {
                char q[80]; snprintf(q, sizeof q, "Remove %s from the list? (Its files stay.)", g->name);
                if (tui_yesno(q)) { memmove(&g_gp[sel], &g_gp[sel + 1], (size_t)(g_ngp - sel - 1) * sizeof(GAMEP)); g_ngp--; dirty = 1; }
            }
            else if (c == K_ENTER || c == K_RIGHT)
            {
                if (g) { fld = 0; continue; }
                if (g_ngp >= MAXGP || sel >= 14) { tui_message("Games", "The list is full (14 games)."); continue; }
                {
                    GAMEP n; char* bs;
                    memset(&n, 0, sizeof n);
                    if (!pick_program(n.exe, sizeof n.exe)) continue;
                    bs = strrchr(n.exe, '\\');
                    snprintf(n.name, sizeof n.name, "%s", bs ? bs + 1 : n.exe);
                    if (strchr(n.name, '.')) *strchr(n.name, '.') = 0;
                    tui_input("Name", "What should the menu call it?", n.name, sizeof n.name, 0);
                    g_gp[g_ngp++] = n; dirty = 1; fld = 0;
                }
            }
        }
        else
        {
            if (c == K_ESC || c == K_LEFT) fld = -1;
            else if (c == K_UP && fld > 0) fld--;
            else if (c == K_DOWN && fld < 4) fld++;
            else if (c == K_ENTER && g)
            {
                if (fld == 0) dirty |= tui_input("Name", "What the menu calls it:", g->name, sizeof g->name, 0);
                else if (fld == 1) dirty |= pick_program(g->exe, sizeof g->exe);
                else if (fld == 2) dirty |= tui_input("Arguments", "Added after the program (empty = none):", g->args, sizeof g->args, 0);
                else if (fld == 3) dirty |= tui_input("SBEMU options for this game", "e.g. /VSCSET:mk1-v1.21 /K:22050  (empty = your saved settings)", g->opts, sizeof g->opts, 1);
                else { pick_cd(g->cd); dirty = 1; }
                for (i = 0; g->name[i]; i++) if (g->name[i] == '|') g->name[i] = ' ';
                for (i = 0; g->args[i]; i++) if (g->args[i] == '|') g->args[i] = ' ';
                for (i = 0; g->opts[i]; i++) if (g->opts[i] == '|') g->opts[i] = ' ';
            }
        }
    }
    if (dirty && !gp_save()) tui_message("Games", "Could not write GAMES.CFG (disk full or write-protected?).");
}
static void anim(unsigned long t)
{
    g_phase = (int)(t % 6);
    draw_header();
    draw_status();          /* the header band includes the Status box: draw it again on top */
}

static void help(void)
{
    static const char* t[] = {
        "\x1B \x1A or Tab        choose a menu on the left",
        "\x18 \x19              choose an item     1-9   jump to item 1-9",
        "Enter            run it              A-Z   next item with that letter",
        "F2 (on a game)   play it with your saved settings, not the tested best",
        "F3               next style: Groovy, Classic, Sunset, Ocean, Neon, Candy,",
        "                 Midnight            F6   animation on / off",
        "F7               letters: Terminus, Terminus bold or the VGA font",
        "F4               display mode: 80x25 ... 80x60, VESA modes",
        "F5               MIDI player (MIDPLAY) from anywhere in the menu",
        "Esc              back to DOS (type MENU to come back)",
        "",
        "Nothing runs by itself: the menu writes your choice to MENUGO.BAT,",
        "exits, and MENU.BAT runs it from plain DOS, then shows the menu again.",
        "Grey items need SBEMU loaded (or not loaded, for Start).",
        "A \x04 after a game: GAMETEST found the SBEMU settings that give it the",
        "best music timing; Play uses them and then goes back to your own.",
        NULL };
    int i;
    uint16_t save[TUI_H][TUI_W];
    void (*a)(unsigned long) = tui_anim;
    memcpy(save, tui_scr, sizeof save);
    tui_anim = 0;
    tui_zoom(4, 2, 74, 22, 0x1F);
    tui_window(4, 2, 74, 22, 0x1F, "Help");
    for (i = 0; t[i]; ++i) tui_put(7, 4 + i, 0x1F, t[i]);
    tui_put(57, 22, 0x1E, "any key: back");
    tui_key();
    tui_anim = a;
    memcpy(tui_scr, save, sizeof save);
}

/* ---------------------------------------------------------------- files */
static int line_colour(const char* s)
{
    const char* c = strstr(s, "clock ");
    if (strstr(s, "LOCKED") || strstr(s, "rror") || strstr(s, "FAIL") || strstr(s, "not found")) return 0x0C;
    if (c)
    {
        double v;
        if (sscanf(c + 6, " %lf%%", &v) == 1) { int k = clk_class(v); return k == 0 ? 0x0A : k == 1 ? 0x0E : 0x0C; }
    }
    if (!strncmp(s, "==", 2) || !strncmp(s, "####", 4)) return 0x0B;
    if (strstr(s, "RESULT") || strstr(s, "REASON") || strstr(s, "Reason")) return 0x0F;
    return 0x07;
}
static void view(const char* name)
{
    char p[300];
    if (strchr(name, '\\')) snprintf(p, sizeof p, "%s", name); else home_path(p, name);
    tui_view_file(p, line_colour);
}

static void save_sel(const ITEM* it)
{
    char p[300], tmp[310]; FILE* f;
    home_path(p, "MENU.CFG");
    if ((f = sw_open(p, tmp, "w")) != NULL) { fprintf(f, "SEL=%s\n", it->label); sw_close(f, tmp, p); }
}
static int load_sel(void)
{
    char p[300], line[80]; FILE* f; int i, sel = 0;
    home_path(p, "MENU.CFG");
    if ((f = fopen(p, "r")) == NULL) return 0;
    if (fgets(line, sizeof line, f) && !strncmp(line, "SEL=", 4))
    {
        line[strcspn(line, "\r\n")] = 0;
        for (i = 0; i < g_n; ++i) if (!strcmp(g_it[i].label, line + 4)) { sel = i; break; }
    }
    fclose(f);
    return sel;
}

/* MENUGO.BAT: what MENU.BAT runs after the menu exits */
static int write_go(const ITEM* it, int saved_only)
{
    char p[300], tmp[310], opts[260] = ""; FILE* f; const char* s;
    const GAMEP* g = it->kind == IT_GAME && it->gp >= 0 && it->gp < g_ngp ? &g_gp[it->gp] : NULL;
    int profile;
    const char* cmd = (it->kind == IT_GAME && saved_only) ? it->cmd2 : it->cmd;
    if (it->kind == IT_GAME && it->best[0] && !saved_only) snprintf(opts, sizeof opts, "%s", it->best);
    if (g && g->opts[0]) snprintf(opts + strlen(opts), sizeof opts - strlen(opts), "%s%s", opts[0] ? " " : "", g->opts);
    profile = opts[0] && g_running;
    home_path(p, "MENUGO.BAT");
    f = sw_open(p, tmp, "wb"); if (!f) return 0;
    fprintf(f, "@ECHO OFF\r\nSET MENUBACK=1\r\n");
    if (it->view[0]) fprintf(f, "SET MENUVIEW=%s\r\n", it->view);
    if (g && g->cd[0])       /* the game's CD image on drive M: first (same steps as CD images > Enter) */
        fprintf(f, "IF NOT \"%%CDVM%%\"==\"1\" CALL C:\\CDSET\\CDON.BAT\r\n"
                   "ECHO Mounting %s on drive M: ...\r\n"
                   "C:\\CDSET\\UCDD.EXE -mount C:\\CDIMAGES\\%s > NUL\r\n"
                   "IF ERRORLEVEL 1 GOTO NOCD\r\n"
                   "ECHO %s> C:\\CDSET\\MOUNTED.FLG\r\n"
                   "GOTO CDOK\r\n"
                   ":NOCD\r\n"
                   "ECHO The CD image %s could not be mounted (see MENU > CD-ROM > CD images).\r\n"
                   "PAUSE\r\n"
                   ":CDOK\r\n", g->cd, g->cd, g->cd, g->cd);
    if (profile) /* the resident SBEMU takes these over (the same way GAMETEST sets its test options) */
        fprintf(f, "ECHO Using this game's SBEMU settings: %s\r\n"
                   "%s\\SBEMU.EXE /VMPU /VSCROM:ROMS %s %s > NUL\r\n", opts, g_home, g_optfile, opts);
    for (s = cmd; *s; )
    {
        const char* e = strchr(s, '|'); int len = e ? (int)(e - s) : (int)strlen(s);
        fprintf(f, "%.*s\r\n", len, s);
        s += len; if (*s == '|') ++s;
    }
    fprintf(f, "%c:\r\nCD %s\r\n", toupper((unsigned char)g_home[0]), g_home + 2);
    if (profile) /* back to the saved settings (defaults first, so the saved ones win) */
        fprintf(f, "%s\\SBEMU.EXE /VMPU /VSCROM:ROMS /VSCAHEAD:50 /VTIMER:1 /VSCFX:1 /VSCAUTO:1 %s > NUL\r\n", g_home, g_optfile);
    if (it->pause) fprintf(f, "ECHO.\r\nPAUSE\r\n");
    return sw_close(f, tmp, p);
}

/* ---------------------------------------------------------------- navigation */
static int first_in(int cat) { int k[80]; return cat_items(cat, k) ? k[0] : -1; }
static int next_cat(int cat, int d)
{
    int i;
    for (i = 0; i < NCAT; i++) { cat = (cat + d + NCAT) % NCAT; if (first_in(cat) >= 0) return cat; }
    return cat;
}
static int move_in(int cat, int sel, int d)
{
    int k[80], n = cat_items(cat, k), i;
    for (i = 0; i < n; i++) if (k[i] == sel) return k[(i + d + n) % n];
    return n ? k[0] : sel;
}
static int jump_letter(int sel, int c)
{
    int k, i;
    c = toupper(c);
    for (k = 1; k <= g_n; ++k)
    {
        const char* l;
        i = (sel + k) % g_n;
        l = g_it[i].label;
        if (!strncmp(l, "Play ", 5) && c != 'P') l += 5;             /* "Play Doom": D jumps there too */
        if (toupper((unsigned char)l[0]) == c) return i;
    }
    return sel;
}

int main(int argc, char** argv)
{
    int c;
    char* s; const char* v;
    if (argc > 1) snprintf(g_home, sizeof g_home, "%s", argv[1]);
    else
    {
        snprintf(g_home, sizeof g_home, "%s", argv[0]);
        for (s = g_home; *s; ++s) if (*s == '/') *s = '\\';
        s = strrchr(g_home, '\\'); if (s) *s = 0; else getcwd(g_home, sizeof g_home);
    }
    for (s = g_home; *s; ++s) { if (*s == '/') *s = '\\'; *s = (char)toupper((unsigned char)*s); }
    if (strlen(g_home) > 3 && g_home[strlen(g_home) - 1] == '\\') g_home[strlen(g_home) - 1] = 0;
    { char p[300]; home_path(p, "MENUGO.BAT"); remove(p); }

    putenv("LFN=n");                       /* 8.3 names: the menu writes them into batch files for plain DOS */
    g_running = sbemu_present();
    read_sbopts();
    read_gametest_summary();
    build_menu();
    g_sel = load_sel();
    g_cat = g_it[g_sel].cat;
    tui_theme_load(g_home);
    tui_init();
    tui_anim = anim;
    /* first run: offer the automatic setup once (TUNED.FLG remembers it was offered) */
    if (!g_running && !home_exists("SBOPTS.TXT") && !home_exists("TUNED.FLG") && !tui_safe)
    {
        int k;
        draw(g_cat, g_sel);
        if (tui_yesno("Welcome! Measure this PC now and choose the best SC-55 settings for it automatically?"
                      " (About a minute. Later: Settings > Automatic setup.)"))
        {
            for (k = 0; k < g_n; k++) if (!strcmp(g_it[k].label, "Automatic setup")) break;
            if (k < g_n && write_go(&g_it[k], 0)) { tui_done(); return 0; }
        }
        else { char p[300]; FILE* f; home_path(p, "TUNED.FLG"); if ((f = fopen(p, "w")) != NULL) { fputs("declined\n", f); fclose(f); } }
    }
    if (tui_safe)
    {
        draw(g_cat, g_sel);
        if (tui_yesno("Safe mode (Shift held): your saved style, display mode and font are not used now. "
                      "Reset them to the plain 80x25 screen for good?"))
        { char p[300]; home_path(p, "THEME.CFG"); remove(p); tui_theme_save(g_home); }
    }
    v = getenv("MENUVIEW");
    if (v && *v) view(v);

    for (;;)
    {
        static int shown_cat = -1;
        if (g_cat != shown_cat && shown_cat >= 0 && TUI_FANCY && tui_animate)
        {   /* a new menu: its items appear one after another */
            int k[80], n = cat_items(g_cat, k);
            for (g_reveal = 0; g_reveal < n; g_reveal++) { draw(g_cat, g_sel); tui_flush(); }
        }
        g_reveal = 99; shown_cat = g_cat;
        draw(g_cat, g_sel);
        c = tui_key();
        if (c == K_ESC) break;
        if (c == K_UP) g_sel = move_in(g_cat, g_sel, -1);
        else if (c == K_DOWN) g_sel = move_in(g_cat, g_sel, 1);
        else if (c == K_LEFT || c == K_STAB || c == K_PGUP) { g_cat = next_cat(g_cat, -1); g_sel = first_in(g_cat); }
        else if (c == K_RIGHT || c == K_TAB || c == K_PGDN) { g_cat = next_cat(g_cat, 1); g_sel = first_in(g_cat); }
        else if (c == K_HOME) g_sel = first_in(g_cat);
        else if (c == K_END) { int k[80], n = cat_items(g_cat, k); if (n) g_sel = k[n - 1]; }
        else if (c == K_F1) help();
        else if (c == 0x13E) tui_pick_mode(g_home);                           /* F4 */
        else if (c == 0x140) { tui_animate = !tui_animate; tui_theme_save(g_home); }   /* F6 */
        else if (c == 0x141) { tui_font = (tui_font + 1) % FONT_COUNT; tui_font_apply(); tui_theme_save(g_home); } /* F7 */
        else if (c == 0x13F)                                                  /* F5: MIDI player */
        {
            int k;
            for (k = 0; k < g_n; k++) if (!strncmp(g_it[k].cmd, "MIDPLAY", 7)) break;
            if (k < g_n && usable(&g_it[k])) { save_sel(&g_it[g_sel]); if (write_go(&g_it[k], 0)) { tui_done(); return 0; } }
            else tui_message("MIDI player", "Start the SC-55 (or SoundFont) first: Sound > Start SC-55 / SoundFont.");
        }
        else if (c == K_F3)
        {
            tui_theme = (tui_theme + 1) % THEME_COUNT;
            tui_theme_save(g_home);
            tui_apply_theme();
            tui_invalidate();
        }
        else if (c >= '1' && c <= '9') { int k[80], n = cat_items(g_cat, k); if (c - '1' < n) g_sel = k[c - '1']; }
        else if (c < 256 && isalpha(c)) { g_sel = jump_letter(g_sel, c); g_cat = g_it[g_sel].cat; }
        else if (c == K_ENTER || c == K_F2)
        {
            ITEM* it = &g_it[g_sel];
            if (c == K_F2 && it->kind != IT_GAME) continue;
            if (!usable(it)) continue;
            save_sel(it);
            if (it->kind == IT_VIEW) { view(it->cmd); continue; }
            if (it->kind == IT_EDIT)
            {
                char keep[40]; int k;
                game_editor();
                snprintf(keep, sizeof keep, "%s", it->label);
                build_menu();
                for (k = 0; k < g_n; k++) if (g_it[k].cat == C_GAMES) { g_sel = k; break; }
                for (k = 0; k < g_n; k++) if (!strcmp(g_it[k].label, keep)) g_sel = k;
                g_cat = g_it[g_sel].cat; tui_invalidate();
                continue;
            }
            if (write_go(it, c == K_F2)) { tui_done(); return 0; }
            tui_message("Error", "Cannot write MENUGO.BAT in the SC55 folder (disk full or write-protected?).");
        }
    }
    save_sel(&g_it[g_sel]);
    tui_done();
    return 0;
}
