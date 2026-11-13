/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* doctor.h - SBDIAG DOCTOR folder body out: the top of BUGREP.TXT. Reads the logs in the SC55 folder (SBLOG, SBDETAIL,
 * SBTRACE, GAMETEST, SBDIAG), writes up to 5 plain-English lines about the likely problems, the versions of every
 * program in the folder, then the rest of the report (body). Included by sbdiag.c; builds on Linux too (tests). */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#ifdef __DJGPP__
#include <dos.h>
#endif

#define DR_MAX 5
static char dr_line[DR_MAX][200];
static int dr_n;
static char dr_dir[200];

static void dr_add(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void dr_add(const char* fmt, ...)
{
    va_list ap; int i;
    if (dr_n >= DR_MAX) return;
    va_start(ap, fmt); vsnprintf(dr_line[dr_n], sizeof dr_line[0], fmt, ap); va_end(ap);
    for (i = 0; i < dr_n; i++) if (!strcmp(dr_line[i], dr_line[dr_n])) return;   /* once */
    dr_n++;
}
/* a text file of the folder, at most 256 KB (the end of bigger ones), NUL-terminated; NULL if missing */
static char* dr_read(const char* name)
{
    char p[300]; FILE* f; long n; char* b;
    snprintf(p, sizeof p, "%s/%s", dr_dir, name);
    if (!(f = fopen(p, "rb"))) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f);
    if (n > 256L * 1024) { fseek(f, n - 256L * 1024, SEEK_SET); n = 256L * 1024; } else fseek(f, 0, SEEK_SET);
    if (n < 0 || !(b = (char*)malloc((size_t)n + 1))) { fclose(f); return NULL; }
    n = (long)fread(b, 1, (size_t)n, f); b[n] = 0; fclose(f);
    return b;
}
/* the line containing p (no CR/LF), cut to n characters */
static void dr_lineof(const char* text, const char* p, char* out, int n)
{
    const char* s = p; int k = 0;
    while (s > text && s[-1] != '\n') s--;
    while (*s == ' ' || *s == '\t') s++;
    while (*s && *s != '\r' && *s != '\n' && k < n - 1) out[k++] = *s++;
    out[k] = 0;
}
static const char* dr_find(const char* t, const char* w) { return t ? strstr(t, w) : NULL; }
static const char* dr_rfind(const char* t, const char* w) { const char *p, *last = NULL; if (!t) return NULL; for (p = t; (p = strstr(p, w)) != NULL; p++) last = p; return last; }
/* the program name of the "Program NAME (" block above p in SBTRACE.TXT */
static void dr_prog_above(const char* t, const char* p, char* out)
{
    const char *s, *best = NULL;
    for (s = t; (s = strstr(s, "Program ")) != NULL && s < p; s++) best = s;
    out[0] = 0;
    if (best) { int k = 0; best += 8; while (best[k] && best[k] != ' ' && k < 8) { out[k] = best[k]; k++; } out[k] = 0; }
    if (!out[0]) strcpy(out, "a game");
}
static int dr_has_roms(void)
{
    char p[300]; DIR* d; struct dirent* e; int n = 0;
    snprintf(p, sizeof p, "%s/ROMS", dr_dir);
    if (!(d = opendir(p))) return -1;
    while ((e = readdir(d)) != NULL)
    {
        const char* x = strrchr(e->d_name, '.');
        if (x && (!strcasecmp(x, ".BIN") || !strcasecmp(x, ".ROM"))) n++;
    }
    closedir(d);
    return n;
}

static void dr_diagnose(void)
{
    char* log = dr_read("SBLOG.TXT"); char* det = dr_read("SBDETAIL.TXT"); char* tr = dr_read("SBTRACE.TXT");
    char* gt = dr_read("GAMETEST.TXT"); char l[160], nm[12]; const char* p; const char* bl = getenv("BLASTER"); int roms;
    if (!log && !det) dr_add("SBEMU has not run yet (no SBLOG.TXT): start the SC-55 from MENU (Sound > Start) first, then make the report again.");
    if ((p = dr_find(log, "Windows is running")) != NULL) dr_add("Windows was running when SBEMU started: SBEMU only works in plain DOS (\"Restart in MS-DOS mode\" or boot to DOS).");
    if ((p = dr_find(log, "no interrupts from the sound card")) != NULL)
        dr_add("The sound card sends no interrupts: no sound and no USB pad. Try another PCI slot or IRQ setting in the BIOS.");
    if ((p = dr_find(log, "Error:")) != NULL || (p = dr_find(log, "Failed")) != NULL) { dr_lineof(log, p, l, 110); dr_add("SBEMU reported an error when it started: \"%s\"", l); }
    if ((p = dr_find(log, "VMPU warning")) != NULL) { dr_lineof(log, p, l, 110); dr_add("The MIDI synth did not load, so music stays silent: \"%s\"", l); }
    roms = dr_has_roms();
    if (roms == 0) dr_add("The ROMS folder has no SC-55 ROM files: the SC-55 cannot play (copy your ROM dumps there, or use a SoundFont).");
    if ((log || det) && (!bl || !*bl)) dr_add("The BLASTER variable is not set: many games will not find the Sound Blaster (start the SC-55 from MENU, it sets it).");
    if ((p = dr_rfind(tr, "never acknowledged")) != NULL)
    { dr_lineof(tr, p, l, 120); dr_prog_above(tr, p, nm); dr_add("A game's Sound Blaster interrupt was never answered (%s): set the game's IRQ to the one in BLASTER (%s).", l[0] ? l + 15 : nm, bl && *bl ? bl : "not set"); }
    if ((p = dr_rfind(tr, "not set up (count")) != NULL)
    { dr_lineof(tr, p, l, 120); dr_add("A game played sound without setting up its DMA channel (%s): set the game's DMA to the one in BLASTER.", l[0] ? l + 15 : "?"); }
    if ((p = dr_rfind(tr, "NOT SUPPORTED by SBEMU")) != NULL)
    { dr_lineof(tr, p, l, 120); dr_prog_above(tr, p, nm); dr_add("%s used Sound Blaster commands SBEMU does not emulate (%s); sound effects may be missing.", nm, l + 24); }
    if ((p = dr_rfind(tr, "NOT SUPPORTED (intelligent")) != NULL)
    { dr_prog_above(tr, p, nm); dr_add("%s uses the MPU-401's \"intelligent mode\", which SBEMU (like most cards) does not have: choose General MIDI / UART in its setup.", nm); }
    if ((p = dr_rfind(tr, "MIDI bytes dropped")) != NULL)
        dr_add("MIDI was lost because the synth could not keep up: lower the SC-55 voices, use a SoundFont, or run SBDIAG while the game plays.");
    if ((p = dr_rfind(tr, "late render")) != NULL)
        dr_add("The sound ran dry (late renders): this PC is too slow for the current synth settings; SBDIAG shows why.");
    if ((p = dr_find(gt, "FAIL")) != NULL) { dr_lineof(gt, p, l, 110); dr_add("GAMETEST found a problem: \"%s\"", l); }
    if (!dr_n) dr_add("No obvious problems found in the logs. Describe what goes wrong (which game, what you hear or see) with this report.");
    free(log); free(det); free(tr); free(gt);
}

/* "NSBVER x" (tools, makefile) or SBEMU/MENU's "1.0.0-beta.N..." inside a program file */
static void dr_version(const char* path, char* out, int n)
{
    static unsigned char buf[32768 + 64]; FILE* f = fopen(path, "rb"); size_t got, keep = 0, i;
    out[0] = 0;
    if (!f) return;
    while ((got = fread(buf + keep, 1, 32768, f)) > 0)
    {
        size_t len = keep + got;
        for (i = 0; i + 12 < len; i++)
        {
            const char* key = NULL;
            if (!memcmp(buf + i, "NSBVER ", 7) && isdigit(buf[i + 7])) key = (const char*)buf + i + 7;
            else if (!out[0] && !memcmp(buf + i, "1.0.0-beta.", 11)) key = (const char*)buf + i;
            if (key)
            {
                int k = 0; while (k < n - 1 && key + k < (const char*)buf + len && isprint((unsigned char)key[k])) { out[k] = key[k]; k++; }
                out[k] = 0;
                if (buf[i] == 'N') { fclose(f); return; }
            }
        }
        keep = len < 64 ? len : 64; memmove(buf, buf + len - keep, keep);
    }
    fclose(f);
}
static int dr_cmp(const void* a, const void* b) { return strcasecmp(*(char* const*)a, *(char* const*)b); }
static void dr_versions(FILE* o)
{
    DIR* d = opendir(dr_dir); struct dirent* e; char* names[200]; int n = 0, i; char* det;
#ifdef __DJGPP__
    { unsigned short tv = _get_dos_version(1), rv = _get_dos_version(0); fprintf(o, "DOS %d.%02d (reported %d.%02d)", tv >> 8, tv & 255, rv >> 8, rv & 255); }
#endif
    fprintf(o, "%s\r\n", getenv("windir") ? ", Windows running" : "");
    if ((det = dr_read("SBDETAIL.TXT")) != NULL) { char l[120]; dr_lineof(det, det, l, 110); fprintf(o, "SBEMU detail log: %s\r\n", l); free(det); }
    if (!d) return;
    while ((e = readdir(d)) != NULL && n < 200)
    {
        const char* x = strrchr(e->d_name, '.');
        if (x && (!strcasecmp(x, ".EXE") || !strcasecmp(x, ".COM") || !strcasecmp(x, ".SYS") || !strcasecmp(x, ".DRV") || !strcasecmp(x, ".VXD"))) names[n++] = strdup(e->d_name);
    }
    closedir(d);
    qsort(names, (size_t)n, sizeof names[0], dr_cmp);
    for (i = 0; i < n; i++)
    {
        char p[300], v[64], tb[20]; struct stat st;
        snprintf(p, sizeof p, "%s/%s", dr_dir, names[i]);
        if (stat(p, &st)) continue;
        strftime(tb, sizeof tb, "%Y-%m-%d %H:%M", localtime(&st.st_mtime));
        dr_version(p, v, sizeof v);
        fprintf(o, "  %-13s %8ld  %s  %s\r\n", names[i], (long)st.st_size, tb, v[0] ? v : "-");
        free(names[i]);
    }
}
/* SBDIAG DOCTOR folder bodyfile outfile */
static int doctor_main(int argc, char** argv)
{
    FILE *o, *b; char buf[4096]; size_t k; int i;
    if (argc < 5) { fprintf(stderr, "SBDIAG DOCTOR folder bodyfile outfile\n"); return 2; }
    snprintf(dr_dir, sizeof dr_dir, "%s", argv[2]);
    if (!(o = fopen(argv[4], "wb"))) return 1;
    dr_diagnose();
    fprintf(o, "==== NUKED SB-EMU bug report - what probably goes wrong (read this first)\r\n");
    for (i = 0; i < dr_n; i++) fprintf(o, "%d. %s\r\n", i + 1, dr_line[i]);
    fprintf(o, "\r\n==== Versions\r\n");
    dr_versions(o);
    fprintf(o, "\r\n");
    if ((b = fopen(argv[3], "rb")) != NULL) { while ((k = fread(buf, 1, sizeof buf, b)) > 0) fwrite(buf, 1, k, o); fclose(b); }
    fclose(o);
    return 0;
}
