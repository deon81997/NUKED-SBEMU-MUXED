/* SBSET - text-mode setup for every SBEMU option the SC-55 pack uses.
 *
 * Writes SBOPTS.TXT ("/A:220 /I:7 ..."), which SC55RUN.BAT and HYBRID.BAT pass to SBEMU as the
 * response file @SBOPTS.TXT, and SBOPTS.BAT ("SET SBOPTS=...") for older launchers. Only options that
 * differ from SBEMU's own defaults are written.
 *
 *   SBSET [folder] [/PAGE:SC|SF|VOL|ADV] default folder C:\SC55; /PAGE opens that sub-menu first
 *
 * Main screen: Sound Blaster, MIDI (port, synth), output rate, and three sub-menus:
 *   SC-55 options        ROM set, volume, reverb/chorus, note limit, speed control
 *   SoundFont options    file, voices, reverb, chorus, song sends
 *   Volume               master, sound effects, FM music, MIDI music (+ SC-55 gain, CD audio)
 *   Timing & advanced    render-ahead, timer give-back, OPL, output jack, CD audio, PM/RM support,
 *                        sound card choice, MIDI out COM port
 * Not here on purpose: /VMPU and /VSCROM (set by the launchers) and the debug switches /DBG /MDBG.
 *
 *   Up/Down choose   Left/Right change   Enter list / open   S or F2 save   D defaults   Esc quit
 *
 * GPL-2.0-or-later. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifndef HOSTTEST
#include <conio.h>
#include <pc.h>
#include <keys.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#endif
#include "safewr.h"
#ifndef HOSTTEST
#include "theme.h"
#endif
#include "../sc55/sc55synth.h"

#define MAXVAL 16
typedef struct { const char* label; const char* opt; const char* vals[MAXVAL]; int n; int def; int cur; const char* hint; const char* labels[MAXVAL]; const char* group; } Field;

enum {
    /* main screen */
    F_PORT, F_IRQ, F_DMA, F_HDMA, F_TYPE,                 /* Sound Blaster */
    F_MPU, F_SYNTH, F_SCMENU, F_SFMENU,                    /* MIDI */
    F_RATE, F_VOLMENU, F_ADVMENU,                          /* Output */
    NMAIN,
    /* SC-55 sub-menu */
    F_ROMSET = NMAIN, F_SCGAIN, F_SCFX, F_SCVOICES, F_SCAUTO,
    /* SoundFont sub-menu */
    F_SF, F_SFVOICES, F_REV, F_ROOM, F_DAMP, F_CHO, F_CRATE, F_CDEPTH, F_SEND,
    /* Timing & advanced sub-menu */
    F_AHEAD, F_VTIMER, F_OPL, F_FM, F_OUT, F_FIXTC, F_VCDA, F_PM, F_RM, F_SC, F_SCFM, F_SCMPU, F_MCOM,
    /* Volume sub-menu (also shows the SC-55 gain and the CD audio volume) */
    F_VOL, F_SBVOL, F_OPLVOL, F_MIDIVOL,
    NF };

#define LV11 { "0", "10", "20", "30", "40", "50", "60", "70", "80", "90", "100" }, 11
#define CARDS { "0", "1", "2", "3", "4" }, 5

static Field F[NF] = {
    { "Port",            "/A",       { "220", "240" }, 2, 0, 0, "Sound Blaster base port. Games: Setup / BLASTER.", {0}, "Sound Blaster" },
    { "IRQ",             "/I",       { "5", "7", "9" }, 3, 1, 1, "Must differ from the IRQs of other cards.", {0}, 0 },
    { "8-bit DMA",       "/D",       { "0", "1", "3" }, 3, 1, 1, "8-bit DMA channel.", {0}, 0 },
    { "16-bit DMA",      "/H",       { "5", "6", "7" }, 3, 0, 0, "16-bit DMA channel (SB16 only).", {0}, 0 },
    { "Type",            "/T",       { "1", "2", "3", "4", "5", "6" }, 6, 4, 4, "1/2/3 = Sound Blaster, 4/5 = Pro, 6 = SB16.", {0}, 0 },
    { "MPU-401 port",    "/P",       { "330", "300", "310", "320", "332", "334" }, 6, 0, 0, "Where games find General MIDI. 330 is the usual one.", {0}, "MIDI" },
    { "MIDI synth",      0,          { "sc55", "sf", "mt32", "cm32l" }, 4, 0, 0, "SC-55 / MT-32 / CM-32L need their ROMs in ROMS; a SoundFont is lightest on the CPU.", {0}, 0 },
    { "SC-55 options",   0,          { "" }, 1, 0, 0, "ROM set, volume, reverb/chorus, note limit, speed control. Enter opens.", {0}, 0 },
    { "SoundFont options", 0,        { "" }, 1, 0, 0, "SoundFont file, voices, reverb, chorus. Enter opens.", {0}, 0 },
    { "Sample rate",     "/K",       { "22050", "32000", "44100", "48000" }, 4, 0, 0, "Output rate. Higher = clearer but heavier: 22050 Hz with the SC-55.", {0}, "Output" },
    { "Volume",          0,          { "" }, 1, 0, 0, "Master, sound effects, FM music, MIDI music, SC-55 and CD audio volume. Enter opens.", {0}, 0 },
    { "Timing & advanced", 0,        { "" }, 1, 0, 0, "Render-ahead, timer give-back, OPL, output jack, CD audio, cards. Enter opens.", {0}, 0 },

    /* SC-55 */
    { "ROM set",         "/VSCSET",  { "auto" }, 1, 0, 0, "ROM sets found (by content) in the ROMS folder. mkII is a bit heavier.", {0}, 0 },
    { "Volume",          "/VSCGAIN", { "100", "50", "75", "125", "150", "200", "300", "400" }, 8, 0, 0, "SC-55 output gain. Raise it if the music is quiet next to the sound effects.", {0}, 0 },
    { "Reverb/chorus",   "/VSCFX",   { "1", "0" }, 2, 0, 0, "Off skips the SC-55's reverb and chorus engine: a bit lighter, sound is dry.", {0}, 0 },
    { "Note limit",      "/VSCVOICES", { "0", "20", "16", "12", "8" }, 5, 0, 0, "Most notes held at once; the oldest is cut first. Saves only ~5% CPU.", {0}, 0 },
    { "Speed control",   "/VSCAUTO", { "1", "0" }, 2, 0, 0, "Turns reverb/chorus off by itself while the CPU cannot keep up.", {0}, 0 },

    /* SoundFont */
    { "SoundFont file",  "/VSF",     { "" }, 1, 0, 0, "Press Enter to browse for a .sf2 file.", {0}, 0 },
    { "Voices",          "/VSFVOICES", { "64", "128", "96", "48", "32", "24", "16", "12", "8" }, 9, 0, 0, "Most voices sounding at once. Fewer = lighter on the CPU.", {0}, 0 },
    { "Reverb level",    "/VSFREV",  LV11, 0, 0, "How much reverb is added. Some SoundFonts already contain reverb.", {0}, 0 },
    { "Reverb room size","/VSFROOM", { "20", "30", "40", "50", "60", "70", "80", "90", "100" }, 9, 4, 4, "Bigger room = longer reverb tail.", {0}, 0 },
    { "Reverb damping",  "/VSFDAMP", LV11, 5, 5, "More damping = darker, softer reverb.", {0}, 0 },
    { "Chorus level",    "/VSFCHO",  LV11, 0, 0, "How much chorus (thicker, wider sound) is added.", {0}, 0 },
    { "Chorus speed",    "/VSFCRATE",{ "3", "5", "8", "10", "15", "20", "30" }, 7, 2, 2, "How fast the chorus sweeps.", {0}, 0 },
    { "Chorus depth",    "/VSFCDEPTH", LV11, 5, 5, "How far the chorus sweeps.", {0}, 0 },
    { "Song sends",      "/VSFSEND", { "1", "0" }, 2, 0, 0, "Follow the song's own reverb/chorus amounts (MIDI CC91/CC93) or use one level.", {0}, 0 },

    /* Timing & advanced */
    { "Render-ahead",    "/VSCAHEAD",{ "50", "0", "25", "75", "100", "150" }, 6, 0, 0, "Music made this far ahead keeps each sound interrupt short. Off = old way.", {0}, 0 },
    { "Timer give-back", "/VTIMER",  { "1", "0" }, 2, 0, 0, "Gives games the timer ticks SBEMU's interrupt held up: keeps MIDI tempo right.", {0}, 0 },
    { "OPL (FM) music",  "/OPL",     { "1", "0" }, 2, 0, 0, "AdLib / OPL3 FM emulation. Off only if a game must not see an OPL.", {0}, 0 },
    { "OPL core",        "/FMCORE",  { "0", "1" }, 2, 0, 0, "The accurate Nuked OPL3 core is heavier.", {0}, 0 },
    { "Output jack",     "/O",       { "1", "0" }, 2, 0, 0, "Which output the card uses (HDA / Xonar cards; AC'97 ignores it).", {0}, 0 },
    { "Sample-rate fix", "/FIXTC",   { "1", "0" }, 2, 0, 0, "Rounds games' Sound Blaster rates to 11/22/44 kHz. Off if sound effects are off-pitch.", {0}, 0 },
    { "CD audio volume", "/VCDA",    { "0", "50", "75", "100", "125", "150", "200" }, 7, 0, 0, "CD-Audio of mounted disc images (needs CDINST's uCDD). HYBRID.BAT sets 100.", {0}, 0 },
    { "Protected mode",  "/PM",      { "1", "0" }, 2, 0, 0, "Sound for protected-mode games (DOS4GW etc.). Keep on.", {0}, 0 },
    { "Real mode",       "/RM",      { "1", "0" }, 2, 0, 0, "Sound for real-mode games: needs JEMM+QPIEMU or QEMM (SC55JEM / SC55QMM).", {0}, 0 },
    { "Sound card",      "/SC",      CARDS, 0, 0, "Which card to play through, from SBEMU /SCL. Only if you have more than one.", {0}, 0 },
    { "FM card",         "/SCFM",    CARDS, 0, 0, "A card with a real OPL chip for FM, from /SCL. 0 = emulate it.", {0}, 0 },
    { "MPU card",        "/SCMPU",   CARDS, 0, 0, "A card with a real MPU-401, from /SCL. 0 = none.", {0}, 0 },
    { "MIDI out port",   "/MCOM",    { "9", "1", "2", "3", "4" }, 5, 0, 0, "Send MIDI to an external synth on a COM port (not with /VMPU, the SC-55).", {0}, 0 },

    /* Volume */
    { "Master volume",   "/VOL",     { "80", "100", "90", "70", "60", "50", "40", "30", "20", "10", "0" }, 11, 0, 0, "The sound card's own output volume (SBEMU sets it at start).", {0}, 0 },
    { "Sound effects",   "/VSBVOL",  { "100", "90", "80", "70", "60", "50", "40", "30", "20", "10", "0" }, 11, 0, 0, "Digital Sound Blaster sound (guns, voices), on top of the game's own mixer.", {0}, 0 },
    { "FM music",        "/VOPLVOL", { "100", "90", "80", "70", "60", "50", "40", "30", "20", "10", "0" }, 11, 0, 0, "OPL / AdLib FM music and sounds, on top of the game's own mixer.", {0}, 0 },
    { "MIDI music",      "/VMIDIVOL",{ "100", "50", "75", "125", "150", "200", "300", "400" }, 8, 0, 0, "General MIDI music (SC-55 or SoundFont). Over 100% makes it louder.", {0}, 0 },
};

static char g_dir[160] = "C:\\SC55";
static char g_sf[200] = "";           /* chosen SoundFont, "" = none */
static int g_roms_found = -1;

static const char* show(const Field* f) { return f->labels[f->cur] ? f->labels[f->cur] : f->vals[f->cur]; }

static void add_rom(const char* name, void* user)
{
    Field* f = (Field*)user;
    if (f->n < MAXVAL)
    {
        char* c = (char*)malloc(strlen(name) + 1);
        strcpy(c, name);
        f->vals[f->n] = c;
        f->labels[f->n] = c;
        f->n++;
    }
}

static void detect_roms(void)
{
    char dir[200];
    sprintf(dir, "%s\\ROMS", g_dir);
    F[F_ROMSET].vals[0] = "auto";
    F[F_ROMSET].labels[0] = "auto (first complete set)";
    F[F_ROMSET].n = 1;
    g_roms_found = SC55_ListInstalledModels(dir, add_rom, &F[F_ROMSET]);
    if (g_roms_found <= 0) F[F_ROMSET].labels[0] = g_roms_found < 0 ? "auto (ROMS folder not readable)" : "auto (no complete set found)";
}

static void onoff(int i, const char* on, const char* off) { F[i].labels[0] = on; F[i].labels[1] = off; }

static void set_labels(void)
{
    static const char* lv[11] = { "Off", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%" };
    static const char* sp[7] = { "0.3 Hz (slow)", "0.5 Hz", "0.8 Hz", "1.0 Hz", "1.5 Hz", "2.0 Hz", "3.0 Hz (fast)" };
    static const char* card[5] = { "0 (default)", "1", "2", "3", "4" };
    int i;
    F[F_RATE].labels[0] = "Low     22050 Hz"; F[F_RATE].labels[1] = "Medium  32000 Hz";
    F[F_RATE].labels[2] = "Good    44100 Hz"; F[F_RATE].labels[3] = "High    48000 Hz";
    F[F_SYNTH].labels[0] = "SC-55 emulation (ROMs)"; F[F_SYNTH].labels[1] = "SoundFont (.sf2 file)";
    F[F_SYNTH].labels[2] = "MT-32 emulation (ROMs)"; F[F_SYNTH].labels[3] = "CM-32L emulation (ROMs)";
    F[F_SF].labels[0] = 0;
    { static const char* g[8] = { "100% (default)", "50%", "75%", "125%", "150%", "200%", "300%", "400%" }; for (i = 0; i < 8; i++) F[F_SCGAIN].labels[i] = g[i]; }
    onoff(F_SCFX, "On (accurate)", "Off (faster, dry sound)");
    F[F_SCVOICES].labels[0] = "Off - all notes (default)"; F[F_SCVOICES].labels[1] = "20 notes"; F[F_SCVOICES].labels[2] = "16 notes";
    F[F_SCVOICES].labels[3] = "12 notes"; F[F_SCVOICES].labels[4] = "8 notes";
    onoff(F_SCAUTO, "On (default)", "Off");
    F[F_SFVOICES].labels[0] = "64 (default)";
    for (i = 0; i < 11; i++) { F[F_REV].labels[i] = lv[i]; F[F_CHO].labels[i] = lv[i]; F[F_DAMP].labels[i] = i == 0 ? "0% (bright)" : lv[i]; F[F_CDEPTH].labels[i] = i == 0 ? "0% (none)" : lv[i]; }
    for (i = 0; i < 9; i++) F[F_ROOM].labels[i] = lv[i + 2];
    for (i = 0; i < 7; i++) F[F_CRATE].labels[i] = sp[i];
    onoff(F_SEND, "Follow the song (CC91/CC93)", "Same level on every channel");
    { static const char* a[6] = { "50 ms (default)", "Off (render in the interrupt)", "25 ms", "75 ms", "100 ms", "150 ms" }; for (i = 0; i < 6; i++) F[F_AHEAD].labels[i] = a[i]; }
    onoff(F_VTIMER, "On (default)", "Off");
    onoff(F_OPL, "On (default)", "Off");
    F[F_FM].labels[0] = "Fast (DOSBox DBOPL)"; F[F_FM].labels[1] = "Accurate (Nuked OPL3)";
    onoff(F_OUT, "Speaker / S/PDIF (default)", "Headphone");
    onoff(F_FIXTC, "On (default)", "Off");
    { static const char* c[7] = { "Off (default)", "50%", "75%", "100%", "125%", "150%", "200%" }; for (i = 0; i < 7; i++) F[F_VCDA].labels[i] = c[i]; }
    onoff(F_PM, "On (default)", "Off");
    onoff(F_RM, "On (default)", "Off");
    {
        static const char* v100[11] = { "100% (default)", "90%", "80%", "70%", "60%", "50%", "40%", "30%", "20%", "10%", "0% (off)" };
        static const char* v80[11] = { "80% (default)", "100%", "90%", "70%", "60%", "50%", "40%", "30%", "20%", "10%", "0% (silent)" };
        static const char* mv[8] = { "100% (default)", "50%", "75%", "125%", "150%", "200%", "300%", "400%" };
        for (i = 0; i < 11; i++) { F[F_VOL].labels[i] = v80[i]; F[F_SBVOL].labels[i] = v100[i]; F[F_OPLVOL].labels[i] = v100[i]; }
        for (i = 0; i < 8; i++) F[F_MIDIVOL].labels[i] = mv[i];
    }
    for (i = 0; i < 5; i++) { F[F_SC].labels[i] = card[i]; F[F_SCFM].labels[i] = card[i]; F[F_SCMPU].labels[i] = card[i]; }
    { static const char* m[5] = { "None - hardware MPU only (default)", "COM1", "COM2", "COM3", "COM4" }; for (i = 0; i < 5; i++) F[F_MCOM].labels[i] = m[i]; }
}

static int using_sf(void) { return F[F_SYNTH].cur == 1; }
static int using_sc55(void) { return F[F_SYNTH].cur == 0; }
static int is_sc_field(int i) { return i >= F_ROMSET && i <= F_SCAUTO; }
static int is_sf_field(int i) { return i >= F_SF && i <= F_SEND; }

static void build_line(char* out)
{
    int i; out[0] = 0;
    for (i = 0; i < NF; i++)
    {
        char t[260];
        if (!F[i].opt) continue;
        if (is_sf_field(i) && !using_sf()) continue;      /* SoundFont settings only with the SoundFont */
        if (is_sc_field(i) && !using_sc55() && !(i == F_SCGAIN && !using_sf())) continue; /* SC-55 settings only with the SC-55 (its gain also sets the MT-32's) */
        if (i == F_MPU && F[F_SYNTH].cur >= 2)           /* MT-32 / CM-32L (Munt) */
        {
            sprintf(t, "%s/VMT32:%s", out[0] ? " " : "", F[F_SYNTH].cur == 2 ? "MT32" : "CM32L");
            strcat(out, t);
        }
        if (i == F_SF)
        {
            if (!g_sf[0]) continue;
            sprintf(t, "%s/VSF:%s", out[0] ? " " : "", g_sf);
            strcat(out, t);
            continue;
        }
        if (i == F_ROMSET) { if (F[i].cur == 0) continue; }   /* auto: SBEMU's own choice */
        else if (F[i].cur == F[i].def) continue;              /* SBEMU's own default: leave it out */
        sprintf(t, "%s%s:%s", out[0] ? " " : "", F[i].opt, F[i].vals[F[i].cur]);
        strcat(out, t);
    }
}

static void parse_tokens(char* s)
{
    char* tok = strtok(s, " \r\n");
    while (tok)
    {
        int i, j;
        if (!strncasecmp(tok, "/VMT32:", 7))
        {
            if (!strcasecmp(tok + 7, "CM32L")) F[F_SYNTH].cur = 3;
            else if (strcasecmp(tok + 7, "0") && strcasecmp(tok + 7, "OFF")) F[F_SYNTH].cur = 2;
        }
        if (!strncasecmp(tok, "/VSF:", 5))
        {
            strncpy(g_sf, tok + 5, sizeof g_sf - 1);
            g_sf[sizeof g_sf - 1] = 0;
            F[F_SYNTH].cur = 1;
        }
        for (i = 0; i < NF; i++)
        {
            size_t ol;
            if (!F[i].opt || i == F_SF) continue;
            ol = strlen(F[i].opt);
            if (!strncasecmp(tok, F[i].opt, ol) && tok[ol] == ':')
                for (j = 0; j < F[i].n; j++)
                    if (!strcasecmp(tok + ol + 1, F[i].vals[j])) F[i].cur = j;
        }
        tok = strtok(NULL, " \r\n");
    }
}

/* read the saved settings: SBOPTS.TXT, or an older SBOPTS.BAT */
static void load(void)
{
    char path[200], line[512]; FILE* f; char* p;
    sprintf(path, "%s\\SBOPTS.TXT", g_dir);
    if ((f = fopen(path, "r")) != NULL)
    {
        while (fgets(line, sizeof line, f)) parse_tokens(line);
        fclose(f);
        return;
    }
    sprintf(path, "%s\\SBOPTS.BAT", g_dir);
    if ((f = fopen(path, "r")) == NULL) return;
    while (fgets(line, sizeof line, f))
        if ((p = strstr(line, "SBOPTS=")) != NULL) parse_tokens(p + 7);
    fclose(f);
}

static int save(void)
{
    char path[200], tmp[210], line[512]; FILE* f;
    build_line(line);
    /* the options as a response file: SC55RUN.BAT passes @SBOPTS.TXT to SBEMU, so the settings never go
     * through the small DOS environment ("Out of environment space"). Written to a temporary file and
     * committed first, so a crash while saving cannot leave a half-written file. */
    sprintf(path, "%s\\SBOPTS.TXT", g_dir);
    f = sw_open(path, tmp, "wb");
    if (!f) return 0;
    fprintf(f, "%s\r\n", line);
    if (!sw_close(f, tmp, path)) return 0;
    sprintf(path, "%s\\SBOPTS.BAT", g_dir);   /* the same as a SET line, for older launchers */
    f = sw_open(path, tmp, "wb");
    if (!f) return 0;
    fprintf(f, "@ECHO OFF\r\nSET SBOPTS=%s\r\n", line);
    return sw_close(f, tmp, path);
}

#ifdef HOSTTEST
int main(int argc, char** argv)
{
    char line[512];
    if (argc > 1) strcpy(g_dir, argv[1]);
    set_labels(); detect_roms(); load();
    build_line(line);
    printf("%s\n", line);
    if (argc > 2) { F[1].cur = 2; save(); }
    return 0;
}
#else

/* ------------------------------------------------------------------ text UI */

#define A_DESK   (TUI_FANCY ? 0x19 : 0x13)   /* pattern on the desk */
#define A_WIN    0x70   /* black on grey */
#define A_BOLD   0x78   /* dark grey on grey */
#define A_GROUP  0x71   /* blue on grey */
#define A_FIELD  0x30   /* black on cyan: value box */
#define A_SELLBL (TUI_FANCY ? 0x5F : 0x1F)   /* selected label (the menus' highlight) */
#define A_SELVAL (TUI_FANCY ? 0x5F : 0x2F)   /* selected value */
#define A_TITLE  0x1F
#define A_BAR    0x30
#define A_KEY    0x3E   /* yellow on cyan */
#define A_DIS    0x78   /* disabled text */
#define A_LIST   0x70
#define A_LISTSEL (TUI_FANCY ? 0x5F : 0x1F)
#define A_ERR    0x4F

static void put(int x, int y, int attr, const char* s)
{
    textattr(attr); gotoxy(x, y); cputs(s);
}

static void fill(int x, int y, int w, int h, int attr, char ch)
{
    char row[100]; int i, n = w > 99 ? 99 : w;
    for (i = 0; i < n; i++) row[i] = ch;
    row[n] = 0;
    for (i = 0; i < h; i++) put(x, y + i, attr, row);
}

/* single/double line frame, interior x+1..x+w-2 */
static void frame(int x, int y, int w, int h, int attr, int dbl)
{
    char row[100]; int i;
    const char tl = dbl ? '\xc9' : '\xda', tr = dbl ? '\xbb' : '\xbf', bl = dbl ? '\xc8' : '\xc0', br = dbl ? '\xbc' : '\xd9';
    const char hz = dbl ? '\xcd' : '\xc4', vt = dbl ? '\xba' : '\xb3';
    int hi = attr, lo = attr;
    if (TUI_FANCY) { hi = (attr & 0xF0) | 0x0F; lo = (attr & 0xF0) | ((attr >> 4) <= 1 ? 0x08 : 0x00); } /* raised 3D edge, as in MENU */
    for (i = 1; i < w - 1; i++) row[i] = hz;
    row[0] = tl; row[w - 1] = 0; put(x, y, hi, row);
    { char c[2] = { tr, 0 }; put(x + w - 1, y, lo, c); }
    row[0] = bl; put(x, y + h - 1, lo, row);
    { char c[2] = { bl, 0 }; put(x, y + h - 1, hi, c); }
    { char c[2] = { br, 0 }; put(x + w - 1, y + h - 1, lo, c); }
    for (i = 1; i < h - 1; i++)
    {
        char c[2]; c[1] = 0;
        c[0] = vt; put(x, y + i, hi, c); put(x + w - 1, y + i, lo, c);
    }
}

static void shadow(int x, int y, int w, int h)
{
    /* darken the cells right of and below a window */
    static unsigned char buf[2 * 100 * 30];
    int i;
    if (x + w + 1 <= 80 && y + h <= 25)
    {
        _conio_gettext(x + w, y + 1, x + w + 1, y + h, buf);
        for (i = 1; i < 2 * 2 * h; i += 2) buf[i] = 0x08;
        puttext(x + w, y + 1, x + w + 1, y + h, buf);
    }
    if (y + h <= 25)
    {
        int cols = w; if (x + 2 + cols > 80) cols = 80 - x - 2;
        if (cols > 0)
        {
            _conio_gettext(x + 2, y + h, x + 1 + cols, y + h, buf);
            for (i = 1; i < 2 * cols; i += 2) buf[i] = 0x08;
            puttext(x + 2, y + h, x + 1 + cols, y + h, buf);
        }
    }
}

typedef struct { int x, y, w, h; unsigned char* save; } Saved;

static void save_area(Saved* s, int x, int y, int w, int h)
{
    int sw = w + 2, sh = h + 1;
    s->x = x; s->y = y; s->w = sw; s->h = sh;
    if (x + sw - 1 > 80) s->w = 80 - x + 1;
    if (y + sh - 1 > 25) s->h = 25 - y + 1;
    s->save = (unsigned char*)malloc((size_t)s->w * s->h * 2);
    if (s->save) _conio_gettext(x, y, x + s->w - 1, y + s->h - 1, s->save);
}

static void restore_area(Saved* s)
{
    if (s->save) { puttext(s->x, s->y, s->x + s->w - 1, s->y + s->h - 1, s->save); free(s->save); s->save = 0; }
}

static void centered(int y, int attr, int x0, int w, const char* s)
{
    int l = (int)strlen(s);
    put(x0 + (w - l) / 2, y, attr, s);
}

/* fit a long path into n characters, keeping the end */
static void fit(char* out, const char* s, int n)
{
    int l = (int)strlen(s);
    if (l <= n) { strcpy(out, s); return; }
    strcpy(out, "...");
    strcat(out, s + l - (n - 3));
}

/* word-wrapped text in w columns x h lines (each line padded to w) */
static void put_wrap(int x, int y, int w, int h, int attr, const char* s)
{
    int line;
    for (line = 0; line < h; line++)
    {
        char buf[120]; int n = (int)strlen(s), cut = n;
        if (n > w) { cut = w; while (cut > w / 2 && s[cut] != ' ') --cut; if (s[cut] != ' ') cut = w; }
        snprintf(buf, sizeof buf, "%-*.*s", w, cut, s);
        put(x, y + line, attr, buf);
        s += cut; while (*s == ' ') ++s;
    }
}
/* wait for the start of a vertical retrace before redrawing, so nothing flickers (bounded) */
static void vsync(void)
{
    long i;
    for (i = 0; i < 100000L && (inportb(0x3DA) & 8); ++i) {}
    for (i = 0; i < 100000L && !(inportb(0x3DA) & 8); ++i) {}
}

/* A drop-down list. items[0..n-1]; returns chosen index or -1 (Esc). */
static int pick_list(int x, int y, const char* title, const char** items, int n, int cur)
{
    int w = (int)strlen(title) + 4, i, rows, top = 0, sel = cur < 0 ? 0 : cur, res = -2;
    Saved sv;
    for (i = 0; i < n; i++) { int l = (int)strlen(items[i]) + 4; if (l > w) w = l; }
    if (w > 70) w = 70;
    rows = n > 12 ? 12 : n;
    if (y + rows + 2 > 24) y = 24 - rows - 2;
    if (y < 2) y = 2;
    if (x + w > 77) x = 77 - w;
    save_area(&sv, x, y, w, rows + 2);
    fill(x, y, w, rows + 2, A_LIST, ' ');
    frame(x, y, w, rows + 2, A_LIST, 0);
    put(x + 2, y, A_LIST, title);
    shadow(x, y, w, rows + 2);
    if (sel >= rows) top = sel - rows + 1;
    while (res == -2)
    {
        int k;
        for (i = 0; i < rows; i++)
        {
            char t[100]; int idx = top + i;
            sprintf(t, " %-*.*s ", w - 4, w - 4, items[idx]);
            put(x + 1, y + 1 + i, idx == sel ? A_LISTSEL : A_LIST, t);
        }
        if (n > rows)
        {
            put(x + w - 1, y + 1, A_LIST, top > 0 ? "\x18" : "\xb3");
            put(x + w - 1, y + rows, A_LIST, top + rows < n ? "\x19" : "\xb3");
        }
        k = getkey();
        switch (k)
        {
        case K_Up: if (sel > 0) sel--; break;
        case K_Down: if (sel < n - 1) sel++; break;
        case K_PageUp: sel -= rows; if (sel < 0) sel = 0; break;
        case K_PageDown: sel += rows; if (sel > n - 1) sel = n - 1; break;
        case K_Home: sel = 0; break;
        case K_End: sel = n - 1; break;
        case K_Return: res = sel; break;
        case K_Escape: res = -1; break;
        default: break;
        }
        if (sel < top) top = sel;
        if (sel >= top + rows) top = sel - rows + 1;
    }
    restore_area(&sv);
    return res;
}

/* Yes / No / Cancel box. returns 'Y', 'N' or 0 (cancel) */
static int ask_save(void)
{
    const int w = 44, h = 7, x = 18, y = 9; Saved sv; int k, r = -1;
    save_area(&sv, x, y, w, h);
    fill(x, y, w, h, A_WIN, ' ');
    frame(x, y, w, h, A_WIN, 1);
    shadow(x, y, w, h);
    centered(y + 2, A_WIN, x, w, "You have unsaved changes.");
    centered(y + 3, A_WIN, x, w, "Save them before leaving?");
    centered(y + 5, A_GROUP, x, w, "[Y] Save    [N] Discard    [Esc] Back");
    while (r < 0)
    {
        k = getkey();
        if (k == 'y' || k == 'Y' || k == K_Return) r = 'Y';
        else if (k == 'n' || k == 'N') r = 'N';
        else if (k == K_Escape) r = 0;
    }
    restore_area(&sv);
    return r;
}

/* ------------------------------------------------------------ file browser */

typedef struct { char name[16]; int dir; unsigned long size; } Ent;

static int ent_cmp(const void* a, const void* b)
{
    const Ent* p = (const Ent*)a; const Ent* q = (const Ent*)b;
    if (p->dir != q->dir) return q->dir - p->dir;
    return strcasecmp(p->name, q->name);
}

static int drive_ok(int letter)
{
    char p[8]; DIR* d;
    sprintf(p, "%c:/", letter);
    d = opendir(p);
    if (!d) return 0;
    closedir(d);
    return 1;
}

static int path_ok(const char* s)
{
    for (; *s; s++) if (*s == ' ' || *s == '=' || *s == '%' || *s == '"' || *s == '<' || *s == '>' || *s == '|' || *s == '^' || *s == '&') return 0;
    return 1;
}

#define MAXENT 600

/* Browse for a .sf2 file. Returns 1 and fills out[] when chosen. */
static int browse_sf(char* out, int outsz, const char* start)
{
    const int w = 64, h = 21, x = 9, y = 3, rows = h - 8;
    static Ent ents[MAXENT];
    char cwd[260], disp[100], msg[80] = "";
    int n = 0, sel = 0, top = 0, i, res = 0, done = 0;
    Saved sv;

    save_area(&sv, x, y, w, h);

    /* starting folder: given path (file or folder), else the SF2 folder, else C:\ */
    cwd[0] = 0;
    if (start && start[0])
    {
        char t[260]; char* sl;
        strncpy(t, start, sizeof t - 1); t[sizeof t - 1] = 0;
        sl = strrchr(t, '\\');
        if (sl && sl > t) { if (sl == t + 2 && t[1] == ':') sl[1] = 0; else *sl = 0; if (chdir(t) == 0) getcwd(cwd, sizeof cwd); }
    }
    if (!cwd[0] && chdir(g_dir) == 0) { char t[260]; sprintf(t, "%s\\SF2", g_dir); if (chdir(t) != 0) chdir(g_dir); getcwd(cwd, sizeof cwd); }
    if (!cwd[0]) { chdir("C:/"); getcwd(cwd, sizeof cwd); }

    fill(x, y, w, h, A_WIN, ' ');
    frame(x, y, w, h, A_WIN, 1);
    shadow(x, y, w, h);
    centered(y, A_TITLE, x, w, " Choose a SoundFont ");

    while (!done)
    {
        DIR* d; struct dirent* e; int k;
        /* read the folder */
        n = 0; sel = 0; top = 0;
        getcwd(cwd, sizeof cwd);
        for (i = 0; cwd[i]; i++) if (cwd[i] == '/') cwd[i] = '\\';
        d = opendir(".");
        if (d)
        {
            while ((e = readdir(d)) != NULL && n < MAXENT)
            {
                struct stat st; size_t l = strlen(e->d_name);
                if (!strcmp(e->d_name, ".") || l > 12) continue;
                if (stat(e->d_name, &st) != 0) continue;
                if (S_ISDIR(st.st_mode))
                {
                    if (!strcmp(e->d_name, "..")) continue;
                    strcpy(ents[n].name, e->d_name); ents[n].dir = 1; ents[n].size = 0; n++;
                }
                else if (l > 4 && !strcasecmp(e->d_name + l - 4, ".sf2"))
                {
                    strcpy(ents[n].name, e->d_name); ents[n].dir = 0; ents[n].size = (unsigned long)st.st_size; n++;
                }
            }
            closedir(d);
            qsort(ents, n, sizeof(Ent), ent_cmp);
        }
        else strcpy(msg, "This folder cannot be read.");

        for (;;)
        {
            /* total list: 0 = [..], 1 = [change drive], 2.. = entries */
            int total = n + 2, idx;
            fill(x + 1, y + 1, w - 2, h - 2, A_WIN, ' ');
            put(x + 2, y + 1, A_BOLD, "Folder:");
            fit(disp, cwd, w - 14);
            put(x + 10, y + 1, A_FIELD, "                                                            ");
            put(x + 10, y + 1, A_FIELD, disp);
            frame(x + 2, y + 3, w - 4, rows + 2, A_WIN, 0);
            for (i = 0; i < rows; i++)
            {
                char t[100], nm[24];
                idx = top + i;
                if (idx >= total) { fill(x + 3, y + 4 + i, w - 6, 1, A_LIST, ' '); continue; }
                if (idx == 0) strcpy(t, " [..]  (parent folder)");
                else if (idx == 1) strcpy(t, " [Change drive...]");
                else
                {
                    Ent* en = &ents[idx - 2];
                    if (en->dir) { sprintf(nm, "[%s]", en->name); sprintf(t, " %-14s  <folder>", nm); }
                    else sprintf(t, " %-14s  %6.1f MB", en->name, en->size / 1048576.0);
                }
                sprintf(nm, "%%-%d.%ds", w - 6, w - 6);
                { char t2[100]; sprintf(t2, nm, t); put(x + 3, y + 4 + i, idx == sel ? A_LISTSEL : A_LIST, t2); }
            }
            if (total > rows)
            {
                put(x + w - 3, y + 4, A_LIST, top > 0 ? "\x18" : " ");
                put(x + w - 3, y + 3 + rows, A_LIST, top + rows < total ? "\x19" : " ");
            }
            {
                char t[100];
                sprintf(t, " %d SoundFont(s) / folder(s) shown. SoundFont files only (.sf2). ", n);
                put(x + 2, y + h - 4, A_BOLD, "                                                            ");
                put(x + 2, y + h - 4, A_BOLD, t);
            }
            put(x + 2, y + h - 3, msg[0] ? A_ERR : A_WIN, "                                                            ");
            if (msg[0]) put(x + 2, y + h - 3, A_ERR, msg);
            put(x + 2, y + h - 2, A_GROUP, " Enter: open / choose   Backspace: up   D: drive   Esc: cancel ");
            msg[0] = 0;

            k = getkey();
            if (k == K_Up) { if (sel > 0) sel--; }
            else if (k == K_Down) { if (sel < total - 1) sel++; }
            else if (k == K_PageUp) { sel -= rows; if (sel < 0) sel = 0; }
            else if (k == K_PageDown) { sel += rows; if (sel > total - 1) sel = total - 1; }
            else if (k == K_Home) sel = 0;
            else if (k == K_End) sel = total - 1;
            else if (k == K_Escape) { done = 1; break; }
            else if (k == K_BackSpace) { if (chdir("..") == 0) break; }
            else if (k == 'd' || k == 'D' || (k == K_Return && sel == 1))
            {
                const char* it[26]; char lab[26][8]; int dn = 0, c, p;
                int map[26];
                put(x + 2, y + h - 3, A_WIN, " Checking drives...                                          ");
                for (c = 'C'; c <= 'Z'; c++)
                    if (drive_ok(c)) { sprintf(lab[dn], "%c:", c); it[dn] = lab[dn]; map[dn] = c; dn++; }
                if (dn == 0) { strcpy(msg, "No drives found."); continue; }
                p = pick_list(x + 4, y + 5, "Drive", it, dn, 0);
                if (p >= 0) { char dp[8]; sprintf(dp, "%c:/", map[p]); if (chdir(dp) == 0) break; strcpy(msg, "That drive is not ready."); }
            }
            else if (k == K_Return)
            {
                if (sel == 0) { if (chdir("..") == 0) break; }
                else if (sel >= 2)
                {
                    Ent* en = &ents[sel - 2];
                    if (en->dir) { if (chdir(en->name) == 0) break; strcpy(msg, "Cannot open that folder."); }
                    else
                    {
                        char full[300];
                        size_t l = strlen(cwd);
                        sprintf(full, "%s%s%s", cwd, (l && cwd[l - 1] == '\\') ? "" : "\\", en->name);
                        for (i = 0; full[i]; i++) if (full[i] >= 'a' && full[i] <= 'z') full[i] -= 32;
                        if (!path_ok(full)) strcpy(msg, "Path has a space or special character. Rename it (8.3).");
                        else if ((int)strlen(full) >= outsz) strcpy(msg, "Path is too long.");
                        else { strcpy(out, full); res = 1; done = 1; break; }
                    }
                }
            }
            if (sel < top) top = sel;
            if (sel >= top + rows) top = sel - rows + 1;
        }
    }
    restore_area(&sv);
    return res;
}

/* -------------------------------------------------------------- main screen */

#define WX 5
#define WY 2
#define WW 70
#define WH 22
#define LBLX (WX + 3)
#define VALX (WX + 22)
#define VALW 38

static int g_dirty = 0;

static int row_of(int i)
{
    /* group headers take a line each: Sound Blaster (5 fields), MIDI (4), Output (3) */
    int r = WY + 2;
    if (i <= F_TYPE) return r + 1 + i;
    r += 1 + 5;
    if (i <= F_SFMENU) return r + 1 + (i - F_MPU);
    r += 1 + 4;
    return r + 1 + (i - F_RATE);
}

static int is_menu(int i) { return i == F_SCMENU || i == F_SFMENU || i == F_VOLMENU || i == F_ADVMENU; }

static int enabled(int i)
{
    if (i == F_SCMENU) return using_sc55();
    if (i == F_SFMENU) return using_sf();
    return 1;
}

static void base_name(char* out, const char* path)
{
    const char* b = strrchr(path, '\\');
    strcpy(out, b ? b + 1 : path);
}

static void summary(int i, char* v)
{
    if (i == F_SCMENU)
        sprintf(v, "%.12s, fx %s%s%s", F[F_ROMSET].cur ? F[F_ROMSET].vals[F[F_ROMSET].cur] : "auto",
                F[F_SCFX].cur ? "off" : "on", F[F_SCVOICES].cur ? ", " : "", F[F_SCVOICES].cur ? show(&F[F_SCVOICES]) : "");
    else if (i == F_SFMENU)
    {
        char b[20];
        if (g_sf[0]) { base_name(b, g_sf); sprintf(v, "%s, %s voices", b, F[F_SFVOICES].vals[F[F_SFVOICES].cur]); }
        else strcpy(v, "(no SoundFont chosen)");
    }
    else if (i == F_VOLMENU)
        sprintf(v, "master %s, effects %s, FM %s, MIDI %s", F[F_VOL].vals[F[F_VOL].cur], F[F_SBVOL].vals[F[F_SBVOL].cur],
                F[F_OPLVOL].vals[F[F_OPLVOL].cur], F[F_MIDIVOL].vals[F[F_MIDIVOL].cur]);
    else
        sprintf(v, "ahead %s ms, give-back %s%s", F[F_AHEAD].vals[F[F_AHEAD].cur], F[F_VTIMER].cur ? "off" : "on",
                F[F_OPL].cur ? ", OPL off" : "");
}

static void draw_row(int i, int sel)
{
    char t[120], v[100]; int y = row_of(i), on = enabled(i), isel = (i == sel);
    if (is_menu(i)) summary(i, v);
    else { strncpy(v, show(&F[i]), sizeof v - 1); v[sizeof v - 1] = 0; }
    sprintf(t, " %-17s", F[i].label);
    put(LBLX - 1, y, !on ? A_DIS : (isel ? A_SELLBL : A_WIN), t);
    sprintf(t, " %-*.*s %c ", VALW - 4, VALW - 4, v, is_menu(i) ? '\x10' : '\x1f');
    put(VALX, y, !on ? A_DIS : (isel ? A_SELVAL : A_FIELD), t);
}

/* a combination that is too heavy for a Pentium M class CPU, or "" */
static const char* warning(void)
{
    if (F[F_SYNTH].cur >= 2) return "MT-32 games: choose \"Roland MT-32 / LAPC-I\" in the game's setup, not General MIDI.";
    if (!using_sf() && F[F_RATE].cur >= 2) return "Heavy: the SC-55 at 44.1/48 kHz is too much for most laptops. Use 22050 Hz.";
    if (!using_sf() && F[F_ROMSET].cur && strstr(F[F_ROMSET].vals[F[F_ROMSET].cur], "mk2")) return "Note: the SC-55mkII ROM set is heavier than the original SC-55.";
    if (F[F_AHEAD].cur == 1) return "Render-ahead is off: long sound interrupts can slow games' music.";
    return "";
}

static void draw_all(int sel, const char* msg)
{
    int i; char line[512], t[100]; const char* w = warning();
    fill(WX, WY, WW, WH, A_WIN, ' ');
    frame(WX, WY, WW, WH, A_WIN, 1);
    put(WX + 2, WY, A_TITLE, " SBEMU Settings ");
    shadow(WX, WY, WW, WH);
    for (i = 0; i < NMAIN; i++)
        if (F[i].group)
        {
            int y = row_of(i) - 1, n = (int)strlen(F[i].group), k; char bar[70];
            put(WX + 2, y, A_GROUP, F[i].group);
            for (k = 0; k < WW - 6 - n; k++) bar[k] = '\xc4';
            bar[k] = 0; put(WX + 3 + n, y, A_BOLD, bar);
        }
    for (i = 0; i < NMAIN; i++) draw_row(i, sel);
    build_line(line);
    put(WX + 2, WY + WH - 5, A_BOLD, "Saved as SBEMU options:");
    fit(t, line[0] ? line : "(none - all SBEMU defaults)", WW - 6);
    sprintf(line, " %-*s", WW - 5, t);
    put(WX + 2, WY + WH - 4, 0x1B, line);
    /* the whole hint (or message / warning), on two lines */
    {
        char h[300];
        if (msg[0]) snprintf(h, sizeof h, "%s", msg);
        else if (w[0]) snprintf(h, sizeof h, "%s  %s", w, F[sel].hint);
        else snprintf(h, sizeof h, "%s", F[sel].hint);
        put_wrap(WX + 2, WY + WH - 3, WW - 4, 2, msg[0] ? 0x2F : (w[0] ? A_ERR : A_BOLD), h);
    }
}

static void draw_bar(void)
{
    textattr(A_TITLE); gotoxy(1, 1); cputs("                                                                               ");
    put(3, 1, A_TITLE, "NUKED SB-EMU");
    put(40, 1, A_TITLE, "Sound Blaster / MIDI / SBEMU setup");
    textattr(A_BAR); gotoxy(1, 25);
    cputs("                                                                               ");
    put(2, 25, A_KEY, "\x18\x19"); put(4, 25, A_BAR, "Select ");
    put(12, 25, A_KEY, "\x1b\x1a"); put(14, 25, A_BAR, "Change ");
    put(22, 25, A_KEY, "Enter"); put(28, 25, A_BAR, "List/Open ");
    put(40, 25, A_KEY, "S"); put(42, 25, A_BAR, "Save ");
    put(49, 25, A_KEY, "D"); put(51, 25, A_BAR, "Defaults ");
    put(60, 25, A_KEY, "T"); put(62, 25, A_BAR, "Auto");
    put(68, 25, A_KEY, "Esc"); put(72, 25, A_BAR, "Quit");
}

/* ---- sub-menus ----------------------------------------------------------------------------- */
typedef struct { int f; const char* head; } MI;   /* f < 0: a heading line */

static const MI M_SC[] = { {-1, "SC-55 emulation"}, {F_ROMSET, 0}, {F_SCGAIN, 0}, {F_SCFX, 0}, {F_SCVOICES, 0}, {F_SCAUTO, 0}, {-2, 0} };
static const MI M_SF[] = { {-1, "SoundFont"}, {F_SF, 0}, {F_SFVOICES, 0}, {-1, "Reverb"}, {F_REV, 0}, {F_ROOM, 0}, {F_DAMP, 0},
                           {-1, "Chorus"}, {F_CHO, 0}, {F_CRATE, 0}, {F_CDEPTH, 0}, {F_SEND, 0}, {-2, 0} };
static const MI M_VOL[] = { {-1, "Volume"}, {F_VOL, 0}, {F_SBVOL, 0}, {F_OPLVOL, 0}, {F_MIDIVOL, 0},
                            {-1, "Also in their own menus"}, {F_SCGAIN, 0}, {F_VCDA, 0}, {-2, 0} };
static const MI M_ADV[] = { {-1, "Timing (game music tempo)"}, {F_AHEAD, 0}, {F_VTIMER, 0},
                            {-1, "Sound"}, {F_OPL, 0}, {F_FM, 0}, {F_OUT, 0}, {F_FIXTC, 0}, {F_VCDA, 0},
                            {-1, "Games and cards"}, {F_PM, 0}, {F_RM, 0}, {F_SC, 0}, {F_SCFM, 0}, {F_SCMPU, 0}, {F_MCOM, 0}, {-2, 0} };

static void sub_value(int f, char* v)
{
    if (f == F_SF) { if (g_sf[0]) fit(v, g_sf, 30); else strcpy(v, "(Enter: browse for a .sf2)"); }
    else { strncpy(v, show(&F[f]), 60); v[60] = 0; }
}

static void sub_menu(const char* title, const MI* m)
{
    int n = 0, rows = 0, i, sel = -1, done = 0, x = 7, w = 66, h, y;
    int ry[32], idx[32], nidx = 0;
    Saved sv;
    while (m[n].f != -2) ++n;
    rows = n;
    h = rows + 7;
    y = (25 - h) / 2 + 1; if (y < 2) y = 2;
    for (i = 0; i < n; i++) { ry[i] = y + 2 + i; if (m[i].f >= 0) { idx[nidx++] = i; if (sel < 0) sel = 0; } }
    save_area(&sv, x, y, w, h);
    fill(x, y, w, h, A_WIN, ' ');
    frame(x, y, w, h, A_WIN, 1);
    shadow(x, y, w, h);
    { char t[80]; sprintf(t, " %s ", title); put(x + 2, y, A_TITLE, t); }
    while (!done)
    {
        char t[120], v[80]; int k, f = m[idx[sel]].f;
        for (i = 0; i < n; i++)
        {
            if (m[i].f < 0) { put(x + 3, ry[i], A_GROUP, m[i].head); continue; }
            sub_value(m[i].f, v);
            sprintf(t, " %-18s", F[m[i].f].label);
            put(x + 3, ry[i], i == idx[sel] ? A_SELLBL : A_WIN, t);
            sprintf(t, " %-*.*s %c ", 34, 34, v, m[i].f == F_SF ? '\x10' : '\x1f');
            put(x + 23, ry[i], i == idx[sel] ? A_SELVAL : A_FIELD, t);
        }
        put_wrap(x + 2, y + h - 4, w - 4, 2, A_BOLD, F[f].hint);
        put(x + 2, y + h - 2, A_GROUP, " \x18\x19 select  \x1b\x1a change  Enter list  Esc back to the main screen ");
        k = getkey();
        vsync();
        switch (k)
        {
        case K_Up: sel = (sel + nidx - 1) % nidx; break;
        case K_Down: sel = (sel + 1) % nidx; break;
        case K_Home: sel = 0; break;
        case K_End: sel = nidx - 1; break;
        case K_Left: if (f != F_SF) { F[f].cur = (F[f].cur + F[f].n - 1) % F[f].n; g_dirty = 1; } break;
        case K_Right: if (f != F_SF) { F[f].cur = (F[f].cur + 1) % F[f].n; g_dirty = 1; } break;
        case K_Return: case ' ':
            if (f == F_SF)
            {
                char out[200];
                if (browse_sf(out, sizeof out, g_sf)) { strcpy(g_sf, out); F[F_SYNTH].cur = 1; g_dirty = 1; }
            }
            else
            {
                const char* items[MAXVAL]; int r;
                for (i = 0; i < F[f].n; i++) items[i] = F[f].labels[i] ? F[f].labels[i] : F[f].vals[i];
                r = pick_list(x + 23, ry[idx[sel]] + 1, F[f].label, items, F[f].n, F[f].cur);
                if (r >= 0 && r != F[f].cur) { F[f].cur = r; g_dirty = 1; }
            }
            break;
        case K_Escape: done = 1; break;
        default: break;
        }
    }
    restore_area(&sv);
}

static void open_menu(int i)
{
    if (i == F_SCMENU) sub_menu("SC-55 options", M_SC);
    else if (i == F_SFMENU)
    {
        if (!using_sf()) { F[F_SYNTH].cur = 1; g_dirty = 1; }   /* opening it means using the SoundFont */
        sub_menu("SoundFont options", M_SF);
        if (!g_sf[0]) F[F_SYNTH].cur = 0;                      /* no file chosen: back to the SC-55 */
    }
    else if (i == F_VOLMENU) sub_menu("Volume", M_VOL);
    else sub_menu("Timing & advanced", M_ADV);
}

static void change(int sel, int dir)
{
    if (is_menu(sel)) return;
    F[sel].cur = (F[sel].cur + F[sel].n + dir) % F[sel].n;
    g_dirty = 1;
}

static void activate(int sel)
{
    if (is_menu(sel)) { open_menu(sel); return; }
    {
        const char* items[MAXVAL]; int i, r;
        for (i = 0; i < F[sel].n; i++) items[i] = F[sel].labels[i] ? F[sel].labels[i] : F[sel].vals[i];
        r = pick_list(VALX, row_of(sel) + 1, F[sel].label, items, F[sel].n, F[sel].cur);
        if (r >= 0 && r != F[sel].cur) { F[sel].cur = r; g_dirty = 1; }
    }
}

/* switching the synth to the SoundFont without a file: ask for one right away */
static void check_sf(int dir)
{
    if (using_sf() && !g_sf[0])
    {
        char out[200];
        if (browse_sf(out, sizeof out, g_sf)) { strcpy(g_sf, out); g_dirty = 1; }
        if (!g_sf[0]) F[F_SYNTH].cur = dir > 0 ? 2 : 0;   /* no file: on to the MT-32, or back to the SC-55 */
    }
}


/* ---- automatic setup (SBSET /TUNE, T on the main screen, offered by MENU the first time) -------------
 * Plays a busy test arrangement through each installed SC-55 ROM set (original and mkII), with and
 * without reverb/chorus, measures how much of the CPU the emulation needs (render time / sound time,
 * uclock), and picks the best-sounding combination that stays under half of the CPU - the game, the
 * Sound Blaster and the FM synth need the rest. Nothing runs resident: SBEMU must not be loaded. */
#include <time.h>
static void tune_feed(double t, double* next, int* beat)
{
    static const int chord[4][4] = { { 60, 64, 67, 72 }, { 57, 60, 64, 69 }, { 53, 57, 60, 65 }, { 55, 59, 62, 67 } };
    static const int prog[9] = { 0, 48, 61, 25, 33, 16, 89, 11, 52 };
    int c, k;
    if (*beat < 0)
    {   /* instruments, volume, effect sends */
        for (c = 0; c < 9; c++) { SC55_PostByte(0xC0 | c); SC55_PostByte(prog[c]); }
        for (c = 0; c < 16; c++)
        {
            SC55_PostByte(0xB0 | c); SC55_PostByte(7); SC55_PostByte(100);
            SC55_PostByte(91); SC55_PostByte(70); SC55_PostByte(93); SC55_PostByte(40);
        }
        *beat = 0; *next = 0;
    }
    while (t >= *next)
    {   /* every 250 ms: new chords on 7 parts, a bass note and drums - about 26 notes sounding */
        const int* ch = chord[(*beat / 4) % 4]; const int* old = chord[((*beat / 4) + 3) % 4];
        if ((*beat & 3) == 0)
            for (c = 0; c < 8; c++) if (c != 4)
                for (k = 0; k < 4; k++)
                {
                    SC55_PostByte(0x90 | c); SC55_PostByte(old[k] + (c & 1) * 12 - 12); SC55_PostByte(0);
                    if (k < (c < 2 ? 4 : 3)) { SC55_PostByte(0x90 | c); SC55_PostByte(ch[k] + (c & 1) * 12 - 12); SC55_PostByte(90); }
                }
        SC55_PostByte(0x94); SC55_PostByte(old[0] - 24); SC55_PostByte(0);
        SC55_PostByte(0x94); SC55_PostByte(ch[0] - 24); SC55_PostByte(110);
        SC55_PostByte(0x99); SC55_PostByte((*beat & 1) ? 38 : 36); SC55_PostByte(120);
        SC55_PostByte(0x99); SC55_PostByte(42); SC55_PostByte(90);
        if ((*beat & 7) == 7) { SC55_PostByte(0x99); SC55_PostByte(49); SC55_PostByte(100); }
        ++*beat; *next += 0.25;
    }
}
/* renders secs of sound at rate; returns the load: CPU time / sound time */
static double tune_measure(double secs, int rate, double* t, double* next, int* beat)
{
    static int16_t buf[1024 * 2];
    long total = (long)(secs * rate), done = 0;
    uclock_t t0 = uclock();
    while (done < total)
    {
        int n = total - done > 512 ? 512 : (int)(total - done);
        tune_feed(*t, next, beat);
        SC55_Render(buf, n, (uint32_t)rate, 0);
        done += n; *t += (double)n / rate;
    }
    return (double)(uclock() - t0) / UCLOCKS_PER_SEC / secs;
}

static int tune(void)
{
    const int x = 8, y = 4, w = 64, h = 18;
    char romdir[200], line[100], best_set[40] = "";
    const char* sets[2] = { NULL, NULL };
    double load[2][2] = { { -1, -1 }, { -1, -1 } };
    int i, ln = y + 4, k, bset = -1, bfx = 1, rate = 0, limit = 0;
    sprintf(romdir, "%s\\ROMS", g_dir);
    for (i = 1; i < F[F_ROMSET].n; i++)
    {
        const char* v = F[F_ROMSET].vals[i];
        if (!sets[0] && !strncasecmp(v, "mk1", 3)) sets[0] = v;
        if (!sets[1] && !strncasecmp(v, "mk2", 3)) sets[1] = v;
    }
    if (!sets[0] && F[F_ROMSET].n > 1) sets[0] = F[F_ROMSET].vals[1];
    fill(x, y, w, h, A_WIN, ' '); frame(x, y, w, h, A_WIN, 1); shadow(x, y, w, h);
    put(x + 2, y, A_TITLE, " Automatic setup ");
    put(x + 2, y + 1, A_WIN, "Measuring how much of this PC's CPU the SC-55 needs. About a");
    put(x + 2, y + 2, A_WIN, "minute: the music is rendered silently, nothing is played.");
    if (!sets[0] && !sets[1])
    {
        put(x + 2, ln, A_ERR, " No complete SC-55 ROM set in the ROMS folder. ");
        put(x + 2, ln + 2, A_WIN, "Copy your SC-55 ROM dumps into ROMS first (see README).");
        put(x + 2, y + h - 2, A_GROUP, "Any key: back");
        getkey();
        return 0;
    }
    for (i = 0; i < 2; i++)
    {
        SC55_Config cfg; double t = 0, next = 0; int beat = -1;
        if (!sets[i]) continue;
        sprintf(line, "%-14s loading ...", sets[i]); put(x + 2, ln, A_WIN, line);
        memset(&cfg, 0, sizeof cfg);
        cfg.romdir = romdir; cfg.romset = sets[i]; cfg.gain_percent = 100; cfg.verbose = 0;
        if (!SC55_Init(&cfg)) { sprintf(line, "%-14s could not be loaded: %.40s", sets[i], SC55_LastError()); put(x + 2, ln, A_ERR, line); ln++; continue; }
        SC55_SetLite(1, 0);
        sprintf(line, "%-14s reverb/chorus on  ...           ", sets[i]); put(x + 2, ln, A_WIN, line);
        tune_measure(1.0, 22050, &t, &next, &beat);                 /* settle */
        load[i][0] = tune_measure(5.0, 22050, &t, &next, &beat);
        sprintf(line, "%-14s reverb/chorus on  %3.0f%% of the CPU", sets[i], load[i][0] * 100); put(x + 2, ln++, A_WIN, line);
        SC55_SetLite(0, 0);
        sprintf(line, "%-14s reverb/chorus off ...           ", sets[i]); put(x + 2, ln, A_WIN, line);
        tune_measure(0.5, 22050, &t, &next, &beat);
        load[i][1] = tune_measure(5.0, 22050, &t, &next, &beat);
        sprintf(line, "%-14s reverb/chorus off %3.0f%% of the CPU", sets[i], load[i][1] * 100); put(x + 2, ln++, A_WIN, line);
        SC55_Shutdown();
    }
    /* the original SC-55 first (the sound most games were written for), effects on before off */
    {
        static const int order[4][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } };
        double lowest = 9; int li = -1;
        for (k = 0; k < 4; k++)
        {
            double l = load[order[k][0]][order[k][1]];
            if (l < 0) continue;
            if (l < lowest) { lowest = l; li = k; }
            if (bset < 0 && l <= 0.50) { bset = order[k][0]; bfx = order[k][1]; }
        }
        if (bset < 0 && li >= 0) { bset = order[li][0]; bfx = order[li][1]; limit = 1; }
    }
    if (bset < 0) { put(x + 2, ln + 1, A_ERR, " No ROM set could be measured. "); getkey(); return 0; }
    {
        double l = load[bset][bfx];
        rate = l <= 0.35 ? 1 : 0;                 /* 32000 Hz only with room to spare */
        if (l > 0.40) limit = 1;                  /* note limit as a safety margin */
        snprintf(best_set, sizeof best_set, "%s", sets[bset]);
        ln++;
        put(x + 2, ln++, A_GROUP, "Best for this PC:");
        sprintf(line, "  ROM set %s, reverb/chorus %s, %s Hz%s", best_set, bfx ? "off" : "on", rate ? "32000" : "22050", limit ? ", note limit 16" : "");
        put(x + 2, ln++, A_BOLD, line);
        if (l > 0.50) put(x + 2, ln++, A_ERR, " Even this needs over half the CPU: a SoundFont would suit this PC better. ");
    }
    put(x + 2, y + h - 2, A_GROUP, "[Y] Use and save these settings    [N] Keep mine");
    for (;;)
    {
        k = getkey();
        if (k == 'n' || k == 'N' || k == K_Escape) return 0;
        if (k == 'y' || k == 'Y' || k == K_Return) break;
    }
    F[F_SYNTH].cur = 0;
    for (i = 0; i < F[F_ROMSET].n; i++) if (!strcmp(F[F_ROMSET].vals[i], best_set)) F[F_ROMSET].cur = i;
    F[F_SCFX].cur = bfx;
    F[F_RATE].cur = rate;
    F[F_SCVOICES].cur = limit ? 2 : 0;
    F[F_SCAUTO].cur = 0;
    return save() ? 1 : -1;
}
static void tune_done_flag(void)
{
    char p[220]; FILE* f;
    sprintf(p, "%s\\TUNED.FLG", g_dir);
    if ((f = fopen(p, "w")) != NULL) { fputs("automatic setup offered\n", f); fclose(f); }
}

int main(int argc, char** argv)
{
    int sel = 0, done = 0, i, page = -1, want_tune = 0; char msg[120] = "";
    putenv("LFN=n"); /* short 8.3 names only: the options file must not contain spaces */
    for (i = 1; i < argc; i++)
    {
        if (!strncasecmp(argv[i], "/PAGE:", 6))
        {
            const char* p = argv[i] + 6;
            if (!strcasecmp(p, "SC")) page = F_SCMENU;
            else if (!strcasecmp(p, "SF")) page = F_SFMENU;
            else if (!strcasecmp(p, "ADV")) page = F_ADVMENU;
            else if (!strcasecmp(p, "VOL")) page = F_VOLMENU;
        }
        else if (!strcasecmp(argv[i], "/TUNE")) want_tune = 1;
        else if (argv[i][0] != '/') { strncpy(g_dir, argv[i], sizeof g_dir - 1); g_dir[sizeof g_dir - 1] = 0; }
    }
    set_labels(); detect_roms(); load();
    if (using_sf() && !g_sf[0]) F[F_SYNTH].cur = 0;
    tui_theme_load(g_dir);
    tui_apply_theme();                 /* the style chosen in MENU (F3) */
    tui_font_apply();
    if (TUI_FANCY && tui_animate) tui_palette_level(0);   /* fade in once the first screen is drawn */
    _setcursortype(_NOCURSOR);
    textattr(A_DESK); clrscr();
    fill(1, 2, 80, 23, A_DESK, '\xb0');
    draw_bar();
    draw_all(sel, msg);
    tui_fade(1);
    {
    int direct = 0;
    if (page >= 0) { sel = page; draw_all(sel, msg); open_menu(page); direct = 1; }
    if (want_tune)
    {
        int r = tune();
        tune_done_flag();
        if (r > 0) strcpy(msg, "Saved the measured settings. Run SC55 (or MENU > Start) to use them.");
        else if (r < 0) strcpy(msg, "Cannot write SBOPTS.TXT (does the folder exist?)");
        if (r > 0) g_dirty = 0;
    }
    while (!done)
    {
        int k;
        if (direct) { direct = 0; k = K_Escape; }   /* opened on a sub-menu from MENU: its Esc closes SETTINGS too */
        else
        {
            draw_all(sel, msg); msg[0] = 0;
            k = getkey();
        }
        vsync();
        switch (k)
        {
        case K_Up: sel = (sel + NMAIN - 1) % NMAIN; break;
        case K_Down: sel = (sel + 1) % NMAIN; break;
        case K_Home: sel = 0; break;
        case K_End: sel = NMAIN - 1; break;
        case K_Left: change(sel, -1); if (sel == F_SYNTH) check_sf(-1); break;
        case K_Right: change(sel, +1); if (sel == F_SYNTH) check_sf(1); break;
        case K_Return: case ' ': activate(sel); if (sel == F_SYNTH) check_sf(0); break;
        case 't': case 'T':
        {
            int r = tune();
            tune_done_flag();
            if (r > 0) { g_dirty = 0; strcpy(msg, "Saved the measured settings."); }
            else if (r < 0) strcpy(msg, "Cannot write SBOPTS.TXT (does the folder exist?)");
            fill(1, 2, 80, 23, A_DESK, '\xb0'); draw_bar();
            break;
        }
        case 'd': case 'D':
            for (i = 0; i < NF; i++) F[i].cur = F[i].def;
            g_sf[0] = 0; g_dirty = 1; strcpy(msg, "Defaults restored (not saved yet).");
            break;
        case 's': case 'S': case K_F2:
            if (using_sf() && !g_sf[0]) { strcpy(msg, "Choose a SoundFont file first (or switch back to the SC-55)."); break; }
            if (save()) { g_dirty = 0; strcpy(msg, "Saved. Restart the PC and run SC55 (or MENU) to use the new settings."); }
            else strcpy(msg, "Cannot write SBOPTS.TXT (does the folder exist?)");
            break;
        case K_Escape: case 'q': case 'Q':
            if (g_dirty)
            {
                int r = ask_save();
                if (r == 'Y') { if (using_sf() && !g_sf[0]) { strcpy(msg, "Choose a SoundFont file first."); break; } if (!save()) { strcpy(msg, "Cannot write SBOPTS.TXT (does the folder exist?)"); break; } done = 1; }
                else if (r == 'N') done = 1;
            }
            else done = 1;
            break;
        default: break;
        }
    }
    }
    tui_fade(0);
    tui_font_restore();
    tui_plain();
    textattr(0x07); clrscr(); _setcursortype(_NORMALCURSOR);
    return 0;
}
#endif
