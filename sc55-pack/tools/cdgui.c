/* CDGUI - the "CD images" screen of the SC-55 pack: pick a disc image in C:\CDIMAGES (or a folder in
 * it) and mount it on the uCDD virtual drive (M:).
 *
 * Nothing is loaded from inside this program: it writes the uCDD command into CDRUN.BAT and exits;
 * GUI.BAT runs it from plain DOS (output into CDOUT.TXT) and starts this screen again, which then
 * shows what happened. Esc goes back to the SC-55 menu.
 *
 *   CDGUI [sc55home] [imagedir] [toolsdir]     defaults C:\SC55 C:\CDIMAGES C:\CDSET
 *
 * For every image it reads, without mounting it:
 *   - the disc label and date (ISO 9660 volume descriptor) and the files in the disc's root folder,
 *     so it can offer to run the disc's INSTALL / SETUP program (I);
 *   - for a .CUE: the tracks (data / CD audio, their lengths) and the BIN it names. The BIN must exist
 *     under its DOS (8.3) name and there must be one BIN; a CUE that names a long file name (very
 *     common: "Game (Track 1).bin") cannot be opened by DOS - F (fix) writes a corrected copy NAME_.CUE
 *     that points to the short name. The original file is never changed.
 * Any folder on any drive can be opened (D: next drive, Backspace: folder above), so images can live
 * on a second disk or a USB drive too. G goes to the home folder, F2 makes the current folder the home
 * folder. The sort order, the last folder, the home folder and the selection are kept in
 * <toolsdir>\CDGUI.CFG. An image outside the images folder is mounted (and remembered) by its full path.
 * An empty or unreadable drive is skipped (the DPMI host answers DOS critical errors with "Fail").
 *
 * GPL-2.0-or-later. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <dir.h>
#include "tui.h"
#include "form3d.h"
#include "crit24.h"

#define MAXIMG 300
#define MAXTR 99
#define MAXROOT 128
enum { T_UP, T_DIR, T_CUE, T_ISO, T_BIN, T_MDM };
static const char* g_tname[] = { "", "DIR", "CUE", "ISO", "BIN", "MDM" };
enum { S_NAME, S_SIZE, S_TYPE, S_LABEL, S_COUNT };
static const char* g_sortname[] = { "name", "size", "type", "label" };

typedef struct {
    char file[13];          /* NAME.EXT (8.3, upper case) */
    int type;
    long size;              /* bytes of the image (CUE: of its BIN) */
    int data, audio;        /* tracks (CUE) */
    int files;              /* FILE lines (CUE) - uCDD takes one */
    char bin[80];           /* the BIN as written in the CUE */
    char other[4][13];      /* more FILEs of the CUE (8.3 names), so they are not listed as images */
    char fixbin[13];        /* a short name that matches it, if the CUE's name does not exist */
    int bin_ok;
    int ndisc;              /* MDM: discs listed */
    int ntr;                /* CUE: tracks with their lengths in sectors (1/75 s) */
    unsigned char tr_audio[MAXTR];
    long tr_len[MAXTR];
    long music;             /* seconds of CD audio */
    char label[33];         /* ISO 9660 volume label, "" if none */
    char date[11];          /* YYYY-MM-DD */
    long fs_off;            /* where the data track's file system starts in the file */
    int sec, hdr;           /* sector size in the file and bytes before the 2048 user bytes */
    int fs_ok;              /* a readable ISO 9660 volume */
    char install[13];       /* INSTALL / SETUP program in the root folder */
} IMG;

typedef struct { char name[13]; long size; int dir; } ROOTENT;

static IMG g_img[MAXIMG];
static int g_n, g_sel, g_top, g_sort, g_nimg, g_ndir;
static char g_home[160] = "C:\\SC55", g_imgdir[160] = "C:\\CDIMAGES", g_tools[160] = "C:\\CDSET";
static char g_cur[160];                /* the folder shown: "D:\\GAMES\\ISO", a drive root is "D:" */
static char g_homedir[160];            /* where G goes (F2 sets it); default the images folder */
static char g_mounted[96], g_label[16];
static int g_cdon;
static int g_phase;
#define BG 1
#define VIS 14                         /* list rows */
#define LX 2                           /* list panel */
#define LW 48
#define DX 51                          /* details panel */
#define DW 29

/* ---------------------------------------------------------------- paths */
static void upcase(char* s) { for (; *s; s++) *s = (char)toupper((unsigned char)*s); }
static void path_of(char* out, size_t sz, const char* dir, const char* f) { snprintf(out, sz, "%s\\%s", dir, f); }
static void cur_dir(char* out, size_t sz) { snprintf(out, sz, "%s%s", g_cur, strlen(g_cur) == 2 ? "\\" : ""); }
static void in_cur(char* out, size_t sz, const char* f) { path_of(out, sz, g_cur, f); }
/* the image's name relative to the images folder (DUKE3D.CUE or GAMES\DUKE3D.CUE, what MOUNTED.FLG and
 * the menu's GAMES.CFG always used), or its full path when it is somewhere else (D:\ISO\DUKE3D.CUE) */
static void rel_of(char* out, size_t sz, const IMG* m)
{
    size_t n = strlen(g_imgdir);
    if (!strcasecmp(g_cur, g_imgdir)) snprintf(out, sz, "%s", m->file);
    else if (!strncasecmp(g_cur, g_imgdir, n) && g_cur[n] == '\\') snprintf(out, sz, "%s\\%s", g_cur + n + 1, m->file);
    else path_of(out, sz, g_cur, m->file);
}
/* full path of a name from MOUNTED.FLG / rel_of */
static void abs_of(char* out, size_t sz, const char* rel)
{
    if (strchr(rel, ':')) snprintf(out, sz, "%s", rel); else path_of(out, sz, g_imgdir, rel);
}
static int dir_ok(const char* d) { return crit_dir_ok(d); }
static void go_dir(const char* d)
{
    snprintf(g_cur, sizeof g_cur, "%s", d);
    upcase(g_cur);
    if (strlen(g_cur) > 2 && g_cur[strlen(g_cur) - 1] == '\\') g_cur[strlen(g_cur) - 1] = 0;
}


static long file_size(const char* path)
{
    struct ffblk ff;
    return findfirst(path, &ff, FA_RDONLY | FA_ARCH | FA_HIDDEN) == 0 ? (long)ff.ff_fsize : -1;
}

/* ---------------------------------------------------------------- CUE sheets */
/* letters and digits only, upper case: "Duke Nukem (Track 1).bin" -> "DUKENUKEMTRACK1BIN" */
static void squash(char* out, size_t sz, const char* s)
{
    size_t n = 0;
    for (; *s && n + 1 < sz; s++) if (isalnum((unsigned char)*s)) out[n++] = (char)toupper((unsigned char)*s);
    out[n] = 0;
}

/* a BIN in the current folder that is the short (8.3) name of the long name "want" */
static int find_short_bin(const char* want, const char* cuebase, char* out)
{
    char w[80], pat[200], b[16], cands[16][13];
    struct ffblk ff; int r, n = 0, i;
    const char* slash = strrchr(want, '\\');
    if (!slash) slash = strrchr(want, '/');
    squash(w, sizeof w, slash ? slash + 1 : want);
    in_cur(pat, sizeof pat, "*.BIN");
    for (r = findfirst(pat, &ff, FA_RDONLY | FA_ARCH); r == 0 && n < 16; r = findnext(&ff)) snprintf(cands[n++], 13, "%s", ff.ff_name);
    /* 1: Windows short name "DUKENU~1.BIN": the part before ~ starts the long name */
    for (i = 0; i < n; i++)
    {
        char* t = strchr(cands[i], '~');
        if (!t) continue;
        snprintf(b, sizeof b, "%.*s", (int)(t - cands[i]), cands[i]);
        squash(b, sizeof b, b);
        if (b[0] && !strncmp(w, b, strlen(b))) { strcpy(out, cands[i]); return 1; }
    }
    /* 2: a BIN with the CUE's own name */
    for (i = 0; i < n; i++)
    {
        char* d = strchr(cands[i], '.');
        if (d && (size_t)(d - cands[i]) == strlen(cuebase) && !strncmp(cands[i], cuebase, strlen(cuebase))) { strcpy(out, cands[i]); return 1; }
    }
    /* 3: the only BIN there is */
    if (n == 1) { strcpy(out, cands[0]); return 1; }
    return 0;
}

static int mode_sector(const char* p, int* hdr)
{
    if (!strncasecmp(p, "AUDIO", 5)) { *hdr = 0; return 2352; }
    if (!strncasecmp(p, "MODE1/2048", 10)) { *hdr = 0; return 2048; }
    if (!strncasecmp(p, "MODE1/2352", 10)) { *hdr = 16; return 2352; }
    if (!strncasecmp(p, "MODE2/2336", 10)) { *hdr = 8; return 2336; }
    if (!strncasecmp(p, "MODE2/2352", 10)) { *hdr = 24; return 2352; }
    *hdr = 16; return 2352;
}

static void read_cue(IMG* m)
{
    char path[200], line[256], base[13];
    long idx[MAXTR];                   /* INDEX 01 of every track, in sectors of the file */
    int secsz[MAXTR], hdr[MAXTR], i;
    FILE* f;
    in_cur(path, sizeof path, m->file);
    snprintf(base, sizeof base, "%.*s", (int)strcspn(m->file, "."), m->file);
    m->size = -1;
    if (!(f = fopen(path, "r"))) return;
    while (fgets(line, sizeof line, f))
    {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (!strncasecmp(p, "FILE", 4) && (p[4] == ' ' || p[4] == '\t'))
        {
            char* q = p + 5, *e;
            while (*q == ' ') q++;
            if (*q == '"') { q++; e = strchr(q, '"'); }
            else e = strpbrk(q, " \t\r\n");
            if (e) *e = 0;
            if (++m->files == 1) snprintf(m->bin, sizeof m->bin, "%s", q);
            else if (m->files <= 5) snprintf(m->other[m->files - 2], 13, "%s", q);
        }
        else if (!strncasecmp(p, "TRACK", 5))
        {
            char* q = p + 5;
            int au;
            while (*q == ' ' || *q == '\t') q++;
            while (*q && *q != ' ' && *q != '\t') q++;          /* track number */
            while (*q == ' ' || *q == '\t') q++;
            au = !strncasecmp(q, "AUDIO", 5);
            if (au) m->audio++; else m->data++;
            if (m->ntr < MAXTR)
            {
                m->tr_audio[m->ntr] = (unsigned char)au;
                secsz[m->ntr] = mode_sector(q, &hdr[m->ntr]);
                idx[m->ntr] = -1;
                m->ntr++;
            }
        }
        else if (!strncasecmp(p, "INDEX", 5) && m->ntr > 0 && m->files == 1)
        {
            int n, mm, ss, ff;
            if (sscanf(p + 5, "%d %d:%d:%d", &n, &mm, &ss, &ff) == 4 && n == 1)
                idx[m->ntr - 1] = ((long)mm * 60 + ss) * 75 + ff;
        }
    }
    fclose(f);
    if (!m->bin[0]) return;
    in_cur(path, sizeof path, m->bin);
    m->size = file_size(path);
    m->bin_ok = m->size >= 0 && strlen(m->bin) <= 12 && !strchr(m->bin, ' ');
    if (!m->bin_ok && find_short_bin(m->bin, base, m->fixbin))
    {
        in_cur(path, sizeof path, m->fixbin);
        m->size = file_size(path);
    }
    /* track lengths: from one INDEX 01 to the next, the last one up to the end of the BIN */
    if (m->files == 1 && m->ntr > 0 && m->size > 0)
    {
        long off = 0;
        for (i = 0; i < m->ntr; i++) if (idx[i] < 0) idx[i] = i ? idx[i - 1] : 0;
        for (i = 0; i < m->ntr; i++)
        {
            long len;
            if (i + 1 < m->ntr) len = idx[i + 1] - idx[i];
            else len = (m->size - off) / secsz[i];
            if (len < 0) len = 0;
            if (i == 0) { m->fs_off = off + idx[0] * secsz[0]; m->sec = secsz[0]; m->hdr = hdr[0]; }
            m->tr_len[i] = len;
            if (m->tr_audio[i]) m->music += len / 75;
            off += len * secsz[i];
        }
        if (m->tr_audio[0]) m->sec = 0;                         /* an audio-only disc has no files */
    }
}

static void read_mdm(IMG* m)
{
    char path[200], line[200]; FILE* f;
    in_cur(path, sizeof path, m->file);
    if (!(f = fopen(path, "r"))) return;
    while (fgets(line, sizeof line, f) && m->ndisc < 10) { char* p = line; while (*p == ' ') p++; if (*p > ' ') m->ndisc++; }
    fclose(f);
}

/* ---------------------------------------------------------------- ISO 9660 inside an image */
/* the file that holds the data (a CUE's BIN) */
static void data_file(const IMG* m, char* out, size_t sz)
{
    if (m->type == T_CUE) in_cur(out, sz, m->bin_ok ? m->bin : m->fixbin[0] ? m->fixbin : m->bin);
    else in_cur(out, sz, m->file);
}
static int read_sector(FILE* f, const IMG* m, long lba, unsigned char* buf)
{
    if (fseek(f, m->fs_off + lba * (long)m->sec + m->hdr, SEEK_SET)) return 0;
    return fread(buf, 1, 2048, f) == 2048;
}
static long le32(const unsigned char* p) { return (long)p[0] | ((long)p[1] << 8) | ((long)p[2] << 16) | ((long)p[3] << 24); }

/* label and date from the primary volume descriptor (sector 16) */
static void probe(IMG* m)
{
    static unsigned char b[2048];
    char path[200];
    FILE* f;
    int i;
    if (m->type == T_ISO) { m->sec = 2048; m->hdr = 0; m->fs_off = 0; }
    else if (m->type == T_BIN)                       /* no CUE: raw 2352-byte sectors or plain 2048 */
    {
        static const unsigned char sync[12] = { 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0 };
        data_file(m, path, sizeof path);
        m->sec = 2048; m->hdr = 0; m->fs_off = 0;
        if ((f = fopen(path, "rb")))
        {
            if (fread(b, 1, 16, f) == 16 && !memcmp(b, sync, 12)) { m->sec = 2352; m->hdr = b[15] == 2 ? 24 : 16; }
            fclose(f);
        }
    }
    else if (m->type != T_CUE || !m->sec || m->size <= 0) return;
    data_file(m, path, sizeof path);
    if (!(f = fopen(path, "rb"))) return;
    if (read_sector(f, m, 16, b) && b[0] == 1 && !memcmp(b + 1, "CD001", 5))
    {
        m->fs_ok = 1;
        memcpy(m->label, b + 40, 32);
        m->label[32] = 0;
        for (i = 31; i >= 0 && (m->label[i] == ' ' || !m->label[i]); i--) m->label[i] = 0;
        if (b[813] >= '1' && b[813] <= '2' && isdigit(b[816]))
            snprintf(m->date, sizeof m->date, "%.4s-%.2s-%.2s", b + 813, b + 817, b + 819);
    }
    fclose(f);
}

/* the files in the disc's root folder */
static int read_root(const IMG* m, ROOTENT* e, int max)
{
    static unsigned char b[2048];
    char path[200];
    FILE* f;
    long lba, len, s;
    int n = 0;
    if (!m->fs_ok) return 0;
    data_file(m, path, sizeof path);
    if (!(f = fopen(path, "rb"))) return 0;
    if (!read_sector(f, m, 16, b)) { fclose(f); return 0; }
    lba = le32(b + 156 + 2);
    len = le32(b + 156 + 10);
    if (len > 16 * 2048) len = 16 * 2048;
    for (s = 0; s * 2048 < len && n < max; s++)
    {
        int p = 0;
        if (!read_sector(f, m, lba + s, b)) break;
        while (p < 2048 && n < max)
        {
            int rl = b[p], nl, k;
            if (!rl || p + rl > 2048) break;
            nl = b[p + 32];
            if (!(nl == 1 && (b[p + 33] == 0 || b[p + 33] == 1)) && nl > 0 && 33 + nl <= rl)
            {
                ROOTENT* r = &e[n++];
                int d = 0;
                for (k = 0; k < nl && d < 12; k++)
                {
                    char ch = (char)b[p + 33 + k];
                    if (ch == ';') break;
                    r->name[d++] = (char)toupper((unsigned char)ch);
                }
                r->name[d] = 0;
                if (d && r->name[d - 1] == '.') r->name[d - 1] = 0;
                r->dir = (b[p + 25] & 2) != 0;
                r->size = le32(b + p + 10);
            }
            p += rl;
        }
    }
    fclose(f);
    return n;
}

static int root_cmp(const void* a, const void* b)
{
    const ROOTENT *x = (const ROOTENT*)a, *y = (const ROOTENT*)b;
    if (x->dir != y->dir) return y->dir - x->dir;
    return strcmp(x->name, y->name);
}

/* INSTALL / SETUP in the root folder */
static void find_installer(IMG* m)
{
    static ROOTENT e[MAXROOT];
    static const char* want[] = { "INSTALL.EXE", "INSTALL.COM", "INSTALL.BAT", "SETUP.EXE", "SETUP.COM", "SETUP.BAT",
                                  "INSTALLD.EXE", "GO.BAT", "GO.EXE" };
    int n = read_root(m, e, MAXROOT), i, k;
    for (k = 0; k < (int)(sizeof want / sizeof want[0]); k++)
        for (i = 0; i < n; i++)
            if (!e[i].dir && !strcmp(e[i].name, want[k])) { snprintf(m->install, sizeof m->install, "%s", want[k]); return; }
}

/* ---------------------------------------------------------------- the list */
static int img_cmp(const void* a, const void* b)
{
    const IMG *x = (const IMG*)a, *y = (const IMG*)b;
    int r = 0;
    if (x->type <= T_DIR || y->type <= T_DIR)
    {
        if (x->type != y->type) return x->type - y->type;   /* "..", then folders, then images */
        return strcmp(x->file, y->file);
    }
    if (g_sort == S_SIZE) r = x->size < y->size ? 1 : x->size > y->size ? -1 : 0;
    else if (g_sort == S_TYPE) r = x->type - y->type;
    else if (g_sort == S_LABEL)
    {
        if (!x->label[0] != !y->label[0]) return !x->label[0] - !y->label[0];
        r = strcmp(x->label, y->label);
    }
    return r ? r : strcmp(x->file, y->file);
}

static void sort_list(void)
{
    char keep[13];
    int i;
    snprintf(keep, sizeof keep, "%s", g_n ? g_img[g_sel].file : "");
    qsort(g_img, (size_t)g_n, sizeof(IMG), img_cmp);
    for (i = 0; i < g_n; i++) if (!strcmp(g_img[i].file, keep)) g_sel = i;
}

static void scan(void)
{
    static const char* pats[] = { "*.CUE", "*.ISO", "*.BIN", "*.MDM" };
    static const int types[] = { T_CUE, T_ISO, T_BIN, T_MDM };
    char pat[200]; struct ffblk ff; int t, r, i, j;
    g_n = 0;
    if (strlen(g_cur) > 2) { memset(&g_img[0], 0, sizeof g_img[0]); strcpy(g_img[0].file, ".."); g_img[0].type = T_UP; g_n = 1; }
    in_cur(pat, sizeof pat, "*.*");
    for (r = findfirst(pat, &ff, FA_DIREC | FA_RDONLY | FA_ARCH); r == 0 && g_n < MAXIMG; r = findnext(&ff))
    {
        IMG* m = &g_img[g_n];
        if (!(ff.ff_attrib & FA_DIREC) || ff.ff_name[0] == '.') continue;
        if (strlen(g_cur) + strlen(ff.ff_name) + 2 > sizeof g_cur) continue;
        memset(m, 0, sizeof *m);
        snprintf(m->file, sizeof m->file, "%s", ff.ff_name);
        upcase(m->file);
        m->type = T_DIR;
        g_n++;
    }
    for (t = 0; t < 4; t++)
    {
        in_cur(pat, sizeof pat, pats[t]);
        for (r = findfirst(pat, &ff, FA_RDONLY | FA_ARCH); r == 0 && g_n < MAXIMG; r = findnext(&ff))
        {
            IMG* m = &g_img[g_n];
            memset(m, 0, sizeof *m);
            snprintf(m->file, sizeof m->file, "%s", ff.ff_name);
            upcase(m->file);
            m->type = types[t]; m->size = (long)ff.ff_fsize;
            if (m->type == T_CUE) read_cue(m);
            if (m->type == T_MDM) read_mdm(m);
            g_n++;
        }
    }
    /* a BIN that belongs to a CUE is mounted through the CUE: do not list it twice */
    for (i = 0; i < g_n; i++)
    {
        if (g_img[i].type != T_BIN) continue;
        for (j = 0; j < g_n; j++)
        {
            const IMG* c = &g_img[j];
            if (c->type != T_CUE) continue;
            if (!strcasecmp(c->bin, g_img[i].file) || !strcasecmp(c->fixbin, g_img[i].file)
                || !strcasecmp(c->other[0], g_img[i].file) || !strcasecmp(c->other[1], g_img[i].file)
                || !strcasecmp(c->other[2], g_img[i].file) || !strcasecmp(c->other[3], g_img[i].file)
                || !strncmp(c->file, g_img[i].file, strcspn(c->file, ".") + 1)) { g_img[i].type = -1; break; }
        }
    }
    for (i = j = 0; i < g_n; i++) if (g_img[i].type >= 0) g_img[j++] = g_img[i];
    g_n = j;
    g_nimg = g_ndir = 0;
    for (i = 0; i < g_n; i++)
    {
        if (g_img[i].type == T_DIR) g_ndir++;
        else if (g_img[i].type > T_DIR) { g_nimg++; probe(&g_img[i]); if (g_img[i].fs_ok) find_installer(&g_img[i]); }
    }
    sort_list();
    if (g_sel >= g_n) g_sel = g_n ? g_n - 1 : 0;
}

static void select_file(const char* name)
{
    int i;
    for (i = 0; i < g_n; i++) if (!strcmp(g_img[i].file, name)) g_sel = i;
}

/* ---------------------------------------------------------------- state */
static void read_state(void)
{
    char path[200]; FILE* f; struct ffblk ff;
    g_mounted[0] = g_label[0] = 0;
    path_of(path, sizeof path, g_tools, "MOUNTED.FLG");
    if ((f = fopen(path, "r")))
    {
        if (fgets(g_mounted, sizeof g_mounted, f)) g_mounted[strcspn(g_mounted, " \r\n")] = 0;
        fclose(f);
        upcase(g_mounted);
    }
    g_cdon = getenv("CDVM") && !strcmp(getenv("CDVM"), "1");
    if (g_cdon && g_mounted[0] && findfirst("M:\\*.*", &ff, FA_LABEL) == 0 && (ff.ff_attrib & FA_LABEL))
    {
        char* d;
        snprintf(g_label, sizeof g_label, "%s", ff.ff_name);
        if ((d = strchr(g_label, '.'))) memmove(d, d + 1, strlen(d));   /* labels come back as 8.3 */
    }
}

/* MOUNTED.FLG holds the path below the images folder (CDIMG.BAT and the menu write the name, maybe
 * without its extension) */
static int is_mounted(const IMG* m)
{
    char rel[96];
    size_t n;
    if (!g_mounted[0] || m->type <= T_DIR) return 0;
    rel_of(rel, sizeof rel, m);
    n = strcspn(rel, ".");
    return !strcmp(g_mounted, rel) || (strlen(g_mounted) == n && !strncmp(g_mounted, rel, n));
}

static void cfg_save(void)
{
    char path[200]; FILE* f;
    path_of(path, sizeof path, g_tools, "CDGUI.CFG");
    if (!(f = fopen(path, "w"))) return;
    fprintf(f, "SORT=%d\nCUR=%s\nHOME=%s\nSEL=%s\n", g_sort, g_cur, g_homedir, g_n ? g_img[g_sel].file : "");
    fclose(f);
}
static void cfg_load(char* sel, size_t sz)
{
    char path[200], line[120]; FILE* f;
    sel[0] = 0;
    path_of(path, sizeof path, g_tools, "CDGUI.CFG");
    if (!(f = fopen(path, "r"))) return;
    while (fgets(line, sizeof line, f))
    {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "SORT=", 5)) { g_sort = atoi(line + 5); if (g_sort < 0 || g_sort >= S_COUNT) g_sort = 0; }
        else if (!strncmp(line, "DIR=", 4) && line[4]) { char d[200]; path_of(d, sizeof d, g_imgdir, line + 4); go_dir(d); }  /* before beta.92 */
        else if (!strncmp(line, "CUR=", 4) && line[4]) go_dir(line + 4);
        else if (!strncmp(line, "HOME=", 5) && line[5]) snprintf(g_homedir, sizeof g_homedir, "%s", line + 5);
        else if (!strncmp(line, "SEL=", 4)) snprintf(sel, sz, "%s", line + 4);
    }
    fclose(f);
    if (!dir_ok(g_homedir)) snprintf(g_homedir, sizeof g_homedir, "%s", g_imgdir);
    if (!dir_ok(g_cur)) go_dir(g_homedir);   /* the last folder is gone (USB drive unplugged?): home */
}

/* ---------------------------------------------------------------- text helpers */
static void size_text(char* out, size_t sz, long b)
{
    if (b < 0) snprintf(out, sz, "    ?  ");
    else if (b >= 1024L * 1024L) snprintf(out, sz, "%4ld MB", (b + 524288L) / 1048576L);
    else snprintf(out, sz, "%4ld KB", (b + 1023) / 1024);
}
static void time_text(char* out, size_t sz, long secs)
{
    if (secs >= 3600) snprintf(out, sz, "%ld:%02ld:%02ld", secs / 3600, secs / 60 % 60, secs % 60);
    else snprintf(out, sz, "%ld:%02ld", secs / 60, secs % 60);
}
static int problem(const IMG* m) { return m->type == T_CUE && (!m->bin_ok || m->files > 1); }

/* ---------------------------------------------------------------- drawing */
static const unsigned char g_rainbow[] = { 9, 11, 10, 14, 12, 13 };

static void draw_header(void)
{
    static const unsigned char plain[] = { 15 };
    char d[200];
    int x;
    tui_fill(1, 1, 80, 5, BG << 4, ' ');
    x = 3 + (TUI_FANCY ? tui_biglogo(3, 1, BG, "CD-ROM", g_rainbow, sizeof g_rainbow, g_phase) : tui_biglogo(3, 1, BG, "CD-ROM", plain, 1, 0));
    tui_put(x + 1, 2, (BG << 4) | 0x0F, "Disc images");
    tui_put(x + 1, 3, (BG << 4) | 0x07, "as drive M:");
    cur_dir(d, sizeof d);
    tui_printf(3, 5, (BG << 4) | 0x0B, "\x1E %.46s", d);

    tui_panel(53, 1, 27, 5, 0, 0x0E, "Drive M:");
    if (!g_cdon) tui_put(55, 2, 0x0C, "\xFE Virtual drive off");
    else if (g_mounted[0]) tui_put(55, 2, 0x0A, "\xFE Disc in the drive");
    else tui_put(55, 2, 0x0E, "\xFE Empty");
    if (g_mounted[0]) tui_printf(55, 3, 0x0F, "%.23s", g_mounted);
    else tui_put(55, 3, 0x08, "Enter mounts a disc");
    if (g_label[0]) tui_printf(55, 4, 0x07, "Label %.17s", g_label);
    else tui_put(55, 4, 0x08, g_cdon ? "U ejects it" : "CDDIAG.BAT checks it");
}

/* a strip of the disc's tracks: data blue, CD audio in alternating yellows */
static void track_strip(int x, int y, int w, const IMG* m)
{
    long total = 0, acc = 0;
    int i, pos = 0;
    for (i = 0; i < m->ntr; i++) total += m->tr_len[i];
    if (total <= 0) return;
    for (i = 0; i < m->ntr; i++)
    {
        int end, a;
        acc += m->tr_len[i];
        end = (int)((acc * w + total / 2) / total);
        if (end <= pos) end = pos + 1;                       /* every track gets at least one cell */
        if (end > w) end = w;
        a = m->tr_audio[i] ? ((i & 1) ? 0x0E : 0x06) : 0x09;
        for (; pos < end; pos++) tui_cell(x + pos, y, a, 0xDB);
    }
}

static void details(const IMG* m)
{
    char t[300], sz[16], tm[16];
    int x = DX + 2, w = DW - 4, y = 9;
    tui_panel(DX, 7, DW, 17, 0, 0x0E, m ? (m->type == T_UP ? "Up" : m->file) : "Details");
    if (!m)
    {
        tui_wrap(x, 9, w, 13, 0x07, "Copy disc images into the images folder, or a folder of their own in it: .CUE with its "
                 ".BIN (games with CD music), .ISO (data only) or .MDM (a list of discs). DOS needs 8.3 names, e.g. "
                 "DUKE3D.CUE. Then press R.");
        return;
    }
    if (m->type == T_UP) { tui_wrap(x, y, w, 4, 0x07, "Back to the folder above (Backspace does the same)."); return; }
    if (m->type == T_DIR)
    {
        tui_put(x, y++, 0x0F, "Folder");
        y++;
        tui_wrap(x, y, w, 5, 0x07, "Enter opens it. Keep each game's images in a folder of its own if you like.");
        return;
    }
    size_text(sz, sizeof sz, m->size);
    switch (m->type)
    {
    case T_CUE: tui_put(x, y, 0x0F, "CUE sheet + BIN"); break;
    case T_ISO: tui_put(x, y, 0x0F, "ISO image"); break;
    case T_BIN: tui_put(x, y, 0x0F, m->sec == 2352 ? "BIN (raw sectors)" : "BIN image"); break;
    case T_MDM: tui_put(x, y, 0x0F, "Disc list (MDM)"); break;
    }
    if (m->type != T_MDM) tui_put(x + w - 7, y, 0x07, sz);
    y += 2;
    if (m->fs_ok)
    {
        tui_printf(x, y++, 0x07, "Label    %.16s", m->label[0] ? m->label : "(none)");
        if (m->date[0]) tui_printf(x, y++, 0x07, "Made     %s", m->date);
    }
    else if (m->type == T_CUE && m->ntr && m->tr_audio[0]) tui_put(x, y++, 0x07, "Audio CD (no files)");
    else if (m->type != T_MDM) tui_put(x, y++, 0x08, "No ISO 9660 files found");

    if (m->type == T_CUE && m->ntr)
    {
        y++;
        track_strip(x, y++, w, m);
        if (m->audio)
        {
            time_text(tm, sizeof tm, m->music);
            tui_printf(x, y++, 0x07, "%d data, %d audio track%s", m->data, m->audio, m->audio == 1 ? "" : "s");
            tui_printf(x, y++, 0x0E, "\x0E %s of CD music", tm);
        }
        else tui_printf(x, y++, 0x07, "%d data track%s", m->data, m->data == 1 ? "" : "s");
    }
    else if (m->type == T_MDM) tui_printf(x, y++, 0x07, "%d disc%s listed", m->ndisc, m->ndisc == 1 ? "" : "s");
    if (m->install[0]) { y++; tui_printf(x, y++, 0x0B, "I runs %s", m->install); }
    y++;

    /* what to know / do about it (rows y .. 21; row 22 says if it is in the drive) */
    {
        int h = 22 - y;
        if (h < 1) h = 1;
        if (m->type == T_CUE)
        {
            if (m->files > 1)
                tui_wrap(x, y, w, h, 0x0C, "\xFE Several BIN files: uCDD needs one. Join them with binmerge.");
            else if (!m->bin_ok && m->fixbin[0])
            {
                snprintf(t, sizeof t, "\xFE DOS cannot open the long BIN name. F writes a copy of the CUE that uses %s.", m->fixbin);
                tui_wrap(x, y, w, h, 0x0E, t);
            }
            else if (!m->bin_ok)
                tui_wrap(x, y, w, h, 0x0C, "\xFE Its BIN is not in this folder. Copy it next to the CUE.");
            else if (m->audio)
                tui_wrap(x, y, w, h, 0x07, "CD music plays through SBEMU (SETTINGS > CD audio volume).");
        }
        else if (m->type == T_ISO) tui_wrap(x, y, w, h, 0x07, "Data only. For CD music use a CUE/BIN copy of the disc.");
        else if (m->type == T_BIN) tui_wrap(x, y, w, h, 0x07, "No CUE: data only. Put the disc's .CUE next to it for music.");
        else if (m->type == T_MDM) tui_wrap(x, y, w, h, 0x07, "Mounts the first disc; swap discs with the uCDD hotkeys (UCDD-DOC.TXT).");
    }
    if (is_mounted(m)) tui_put(x, 22, 0x0A, "\xFE In drive M: now");
}

static void draw(void)
{
    int i, y;
    char hdr[64];
    tui_clear(BG << 4);
    draw_header();
    snprintf(hdr, sizeof hdr, "Images  \xFA  sorted by %s", g_sortname[g_sort]);
    tui_panel(LX, 7, LW, 17, 7, 0x1F, hdr);
    { char h[64]; snprintf(h, sizeof h, "%-3s%-13s%-5s%-13s%7s", "", "Name", "Type", "Label", "Size"); tui_putw(LX + 1, 8, LW - 3, 0x78, h); }
    if (!g_n) tui_put(LX + 4, 10, 0x74, "(no disc images here)");
    if (g_sel < g_top) g_top = g_sel;
    if (g_sel >= g_top + VIS) g_top = g_sel - VIS + 1;
    for (i = 0, y = 9; i < VIS && g_top + i < g_n; i++, y++)
    {
        const IMG* m = &g_img[g_top + i];
        int on = g_top + i == g_sel, a = on ? (TUI_FANCY ? 0x5F : 0x1F) : 0x70, bg = a & 0xF0;
        char b[64], sz[16];
        if (m->type == T_UP) snprintf(b, sizeof b, "   ..          (up)");
        else if (m->type == T_DIR) snprintf(b, sizeof b, "   %-12s <DIR>", m->file);
        else
        {
            size_text(sz, sizeof sz, m->size);
            snprintf(b, sizeof b, "   %-12s %-4s %-12.12s %7s", m->file, g_tname[m->type], m->label, m->type == T_MDM ? "" : sz);
        }
        tui_putw(LX + 1, y, LW - 3, a, b);
        /* icon: folder, problem, CD music, data disc */
        if (m->type <= T_DIR) tui_cell(LX + 2, y, bg | (on ? 0x0E : 0x06), m->type == T_UP ? 0x18 : 0x1E);
        else if (problem(m)) tui_cell(LX + 2, y, bg | (m->fixbin[0] && m->files <= 1 ? 0x0E : 0x0C), '!');
        else if (m->type == T_CUE && m->audio) tui_cell(LX + 2, y, bg | (on ? 0x0B : 0x01), 0x0E);
        else tui_cell(LX + 2, y, bg | (on ? 0x0F : 0x08), 0x09);
        if (m->type > T_DIR)                                 /* type in its own colour */
            tui_put(LX + 17, y, bg | (on ? 0x0F : m->type == T_CUE ? 0x01 : m->type == T_ISO ? 0x02 : m->type == T_MDM ? 0x05 : 0x08), g_tname[m->type]);
        if (is_mounted(m)) tui_put(LX + LW - 5, y, bg | (on ? 0x0A : 0x02), "M:");
        if (m->install[0] && !is_mounted(m)) tui_cell(LX + LW - 4, y, bg | (on ? 0x0B : 0x03), 'I');
        if (on) tui_cell(LX, y, 0x7D, 0x10);
    }
    /* scroll bar */
    if (g_n > VIS)
    {
        int th = VIS * VIS / g_n, tp;
        if (th < 1) th = 1;
        tp = (g_n - VIS) ? (VIS - th) * g_top / (g_n - VIS) : 0;
        for (i = 0; i < VIS; i++) tui_cell(LX + LW - 2, 9 + i, 0x78, i >= tp && i < tp + th ? 0xDB : 0xB0);
    }
    details(g_n ? &g_img[g_sel] : NULL);

    tui_fill(1, 24, 80, 1, BG << 4, ' ');
    {
        int x = 3;
        x += tui_printf(x, 24, (BG << 4) | 0x07, "%d image%s", g_nimg, g_nimg == 1 ? "" : "s");
        if (g_ndir) x += tui_printf(x, 24, (BG << 4) | 0x07, ", %d folder%s", g_ndir, g_ndir == 1 ? "" : "s");
        x += tui_put(x + 2, 24, (BG << 4) | 0x0E, "\x0E") + 2;
        x += tui_put(x + 1, 24, (BG << 4) | 0x07, "music") + 1;
        x += tui_put(x + 2, 24, (BG << 4) | 0x0C, "!") + 2;
        x += tui_put(x + 1, 24, (BG << 4) | 0x07, "needs a fix") + 1;
        x += tui_put(x + 2, 24, (BG << 4) | 0x0B, "I") + 2;
        tui_put(x + 1, 24, (BG << 4) | 0x07, "installer");
        tui_printf(62, 24, (BG << 4) | 0x0B, "%s \xFA %dx%d", tui_theme_name[tui_theme], tui_cols, tui_rows);
    }
    tui_fill(1, 25, 80, 1, 0x30, ' ');
    tui_put(2, 25, 0x30, "Enter mount  D drive  G home  F2 set home  I install  U eject  F1 help  Esc");
}

static void anim(unsigned long t) { g_phase = (int)(t % 6); draw_header(); }

/* ---------------------------------------------------------------- 3D screen (beta.77, form3d.h)
 * On while MENU3D.CFG exists (the 3D menu is on); F8 switches. The help and the disc's file list are text
 * screens: the 3D mode is left for them and set again afterwards. */
static int g3;
static F3VIEW c3_v;
static void c3_build(F3VIEW* v);
static void ui_message(const char* t, const char* m) { if (g3) { c3_build(&c3_v); f3_message(&c3_v, t, m); } else tui_message(t, m); }
static int ui_yesno(const char* q) { if (g3) { c3_build(&c3_v); return f3_yesno(&c3_v, q); } return tui_yesno(q); }
static void c3_text_begin(void) { f3_leave(); tui_init(); draw(); }
static void c3_text_end(void) { tui_done(); if (!f3_enter()) { g3 = 0; tui_init(); } }

static void c3_details(const IMG* m, char* o, size_t n)
{
    char sz[16], tm[16]; size_t k = 0;
#define C3P(...) do { if (k < n) k += (size_t)snprintf(o + k, n - k, __VA_ARGS__); } while (0)
    o[0] = 0;
    if (!m) { C3P("Copy disc images into the images folder, or a folder of their own in it: .CUE with its .BIN (games with CD music), .ISO (data only) or .MDM (a list of discs). DOS needs 8.3 names, e.g. DUKE3D.CUE. Then press R."); return; }
    if (m->type == T_UP) { C3P("Back to the folder above (Backspace does the same)."); return; }
    if (m->type == T_DIR) { C3P("Folder. Enter opens it. Keep each game's images in a folder of its own if you like."); return; }
    size_text(sz, sizeof sz, m->size);
    { char* t = sz; while (*t == ' ') t++; memmove(sz, t, strlen(t) + 1); }
    C3P("%s", m->type == T_CUE ? "CUE sheet + BIN" : m->type == T_ISO ? "ISO image" : m->type == T_MDM ? "Disc list (MDM)" : m->sec == 2352 ? "BIN (raw sectors)" : "BIN image");
    if (m->type != T_MDM) C3P(", %s", sz);
    if (m->fs_ok) { C3P(". Label %s", m->label[0] ? m->label : "(none)"); if (m->date[0]) C3P(", made %s", m->date); }
    else if (m->type == T_CUE && m->ntr && m->tr_audio[0]) C3P(". Audio CD (no files)");
    if (m->type == T_CUE && m->ntr)
    {
        if (m->audio) { time_text(tm, sizeof tm, m->music); C3P(". %d data + %d audio tracks, %s of CD music", m->data, m->audio, tm); }
        else C3P(". %d data track%s", m->data, m->data == 1 ? "" : "s");
    }
    else if (m->type == T_MDM) C3P(". %d disc%s listed", m->ndisc, m->ndisc == 1 ? "" : "s");
    if (m->install[0]) C3P(". I runs %s", m->install);
    C3P(". ");
    if (m->type == T_CUE)
    {
        if (m->files > 1) C3P("Several BIN files: uCDD needs one. Join them with binmerge.");
        else if (!m->bin_ok && m->fixbin[0]) C3P("DOS cannot open the long BIN name. F writes a copy of the CUE that uses %s.", m->fixbin);
        else if (!m->bin_ok) C3P("Its BIN is not in this folder. Copy it next to the CUE.");
        else if (m->audio) C3P("CD music plays through SBEMU (SETTINGS > CD audio volume).");
    }
    else if (m->type == T_ISO) C3P("Data only. For CD music use a CUE/BIN copy of the disc.");
    else if (m->type == T_BIN) C3P("No CUE: data only. Put the disc's .CUE next to it for music.");
    else if (m->type == T_MDM) C3P("Mounts the first disc; swap discs with the uCDD hotkeys (UCDD-DOC.TXT).");
    if (is_mounted(m)) C3P("  In drive M: now.");
#undef C3P
}
static void c3_build(F3VIEW* v)
{
    int i; char d[200], b[80], sz[16];
    f3_clear(v);
    v->app = "CD IMAGES"; v->accent = 0xE0A020; v->ntab = 0;
    cur_dir(d, sizeof d);
    snprintf(v->title, sizeof v->title, "%.40s  (sorted by %s)", d, g_sortname[g_sort]);
    if (!g_cdon) { snprintf(v->status, sizeof v->status, "Drive M: off"); v->statcol = 0xFF6050; }
    else if (g_mounted[0]) { snprintf(v->status, sizeof v->status, "M: %.24s", g_mounted); v->statcol = 0x60FF80; }
    else { snprintf(v->status, sizeof v->status, "Drive M: empty"); v->statcol = 0xFFD060; }
    for (i = 0; i < g_n; i++)
    {
        const IMG* m = &g_img[i]; F3ROW* r;
        if (m->type == T_UP) { r = f3_add(v, F3_ITEM, "..", "folder above"); r->mark = F3M_UP; continue; }
        if (m->type == T_DIR) { r = f3_add(v, F3_ITEM, m->file, "folder"); r->mark = F3M_FOLDER; continue; }
        size_text(sz, sizeof sz, m->size);
        snprintf(b, sizeof b, "%s%-4s %-12.12s %s%s", is_mounted(m) ? "M: " : "", g_tname[m->type], m->label, m->type == T_MDM ? "" : sz,
                 m->install[0] && !is_mounted(m) ? "  I" : "");
        r = f3_add(v, F3_ITEM, m->file, b);
        if (problem(m)) { r->mark = F3M_WARN; r->col = m->fixbin[0] && m->files <= 1 ? 0xFFD060 : 0xFF7060; }
        else if (m->type == T_CUE && m->audio) { r->mark = F3M_MUSIC; r->col = 0x70D0FF; }
        else r->mark = F3M_DISC;
        if (is_mounted(m)) r->col = 0x70FF90;
    }
    if (!g_n) f3_add(v, F3_ITEM, "(no disc images here)", "");
    v->sel = g_n ? g_sel : 0;
    c3_details(g_n ? &g_img[g_sel] : NULL, v->info, sizeof v->info);
    v->infocol = g_n && problem(&g_img[g_sel]) ? 0xFFC060 : 0xD8E0F0;
    snprintf(v->foot, sizeof v->foot, "%d image%s, %d folder%s   %s", g_nimg, g_nimg == 1 ? "" : "s", g_ndir, g_ndir == 1 ? "" : "s",
             g_label[0] ? "" : "");
    if (g_label[0]) snprintf(v->foot + strlen(v->foot), sizeof v->foot - strlen(v->foot), "disc in M: labelled %s", g_label);
    v->keys = "Enter mount  D drive  G home  F2 set home  I install  U eject  F1 help  F8 text";
}

/* ---------------------------------------------------------------- pop-ups */
static void help(void)
{
    static const char* lines[] = {
        "\x18\x19 PgUp PgDn Home End   choose",
        "Enter       mount the disc (or open a folder)",
        "Backspace   back to the folder above",
        "D           next drive (second disk, USB drive...)",
        "G / F2      go to / set the home folder",
        "I           mount and run the disc's INSTALL/SETUP",
        "C           list the files on the disc",
        "U           eject the disc in drive M:",
        "F           fix a CUE whose BIN DOS cannot open",
        "S           sort by name / size / type / label",
        "R           read the folder again",
        "F3 style  F4 screen  F6 animation  F7 font",
        "Esc         back to the menu",
        "",
        "\x0E = CD music (plays through SBEMU)",
        "! = needs attention    I = has an installer",
        "M: = in the drive now" };
    int n = (int)(sizeof lines / sizeof lines[0]), i, x = 13, w = 56, h = n + 4, y = 5;
    uint16_t save[TUI_H][TUI_W];
    void (*a)(unsigned long) = tui_anim;
    tui_anim = NULL;
    memcpy(save, tui_scr, sizeof save);
    tui_zoom(x, y, w, h, 0x1F);
    tui_window(x, y, w, h, 0x1F, "CD images - keys");
    for (i = 0; i < n; i++) tui_put(x + 3, y + 2 + i, i >= 14 ? 0x17 : 0x1F, lines[i]);
    tui_key();
    memcpy(tui_scr, save, sizeof save);
    tui_anim = a;
}

static void contents(const IMG* m)
{
    static ROOTENT e[MAXROOT];
    int n, top = 0, c, i, x = 11, w = 60, h = 19, rows = h - 6, y0 = 5;
    void (*a)(unsigned long) = tui_anim;
    long total = 0;
    char title[64];
    uint16_t save[TUI_H][TUI_W];
    if (m->type <= T_DIR) return;
    if (!m->fs_ok) { tui_message("Contents", "No ISO 9660 file system was found in this image (an audio CD, or a format this screen cannot read)."); return; }
    n = read_root(m, e, MAXROOT);
    qsort(e, (size_t)n, sizeof e[0], root_cmp);
    for (i = 0; i < n; i++) if (!e[i].dir) total += e[i].size;
    snprintf(title, sizeof title, "%s  \xFA  %s", m->file, m->label[0] ? m->label : "no label");
    memcpy(save, tui_scr, sizeof save);
    tui_anim = NULL;
    tui_zoom(x, y0, w, h, 0x1F);
    for (;;)
    {
        char sz[16];
        tui_window(x, y0, w, h, 0x1F, title);
        tui_printf(x + 3, y0 + 2, 0x1E, "Root folder: %d entr%s, %ld MB in files", n, n == 1 ? "y" : "ies", (total + 524288L) / 1048576L);
        for (i = 0; i < rows; i++)
        {
            int k = top + i, a;
            if (k >= n) break;
            a = e[k].dir ? 0x1E : !strcmp(e[k].name, m->install) ? 0x1B : 0x1F;
            if (e[k].dir) tui_printf(x + 3, y0 + 4 + i, a, "\x1E %-12s  <DIR>", e[k].name);
            else { size_text(sz, sizeof sz, e[k].size); tui_printf(x + 3, y0 + 4 + i, a, "  %-12s %s", e[k].name, sz); }
        }
        if (top > 0) tui_cell(x + w - 3, y0 + 4, 0x1E, 0x18);
        if (top + rows < n) tui_cell(x + w - 3, y0 + 4 + rows - 1, 0x1E, 0x19);
        tui_put(x + 3, y0 + h - 2, 0x17, "\x18\x19 scroll   any other key closes");
        c = tui_key();
        if (c == K_UP) { if (top > 0) top--; }
        else if (c == K_DOWN) { if (top + rows < n) top++; }
        else if (c == K_PGUP) { top -= rows; if (top < 0) top = 0; }
        else if (c == K_PGDN) { top += rows; if (top > n - rows) top = n > rows ? n - rows : 0; }
        else break;
        memcpy(tui_scr, save, sizeof save);
    }
    memcpy(tui_scr, save, sizeof save);
    tui_anim = a;
}

/* ---------------------------------------------------------------- actions */
static int write_run(const char* body)
{
    char path[200]; FILE* f;
    path_of(path, sizeof path, g_tools, "CDRUN.BAT");
    if (!(f = fopen(path, "wb"))) return 0;
    fputs("@ECHO OFF\r\n", f);
    fputs(body, f);
    fclose(f);
    return 1;
}

/* mount the image; with run != NULL also start that program from the disc's root folder */
static int mount(const IMG* m, const char* run)
{
    char body[1600], rel[96], img[200], prog[200] = "", eject_old[300] = "";
    int n;
    rel_of(rel, sizeof rel, m);
    if (g_mounted[0] && !is_mounted(m))        /* uCDD has one drive: take the other disc out first */
        snprintf(eject_old, sizeof eject_old, "%s\\UCDD.EXE -unmount > %s\\CDOUT.TXT\r\n"
                 "IF ERRORLEVEL 1 GOTO STUCK\r\n"
                 "IF EXIST %s\\MOUNTED.FLG DEL %s\\MOUNTED.FLG\r\n", g_tools, g_tools, g_tools, g_tools);
    abs_of(img, sizeof img, rel);
    if (run)
    {
        const char* ext = strrchr(run, '.');
        snprintf(prog, sizeof prog,
            "ECHO Starting %s from drive M: ...\r\n"
            "M:\r\n"
            "CD \\\r\n"
            "%s%s\r\n"
            "C:\r\n"
            "ECHO RUN %s> %s\\CDRES.TXT\r\n", run, ext && !strcasecmp(ext, ".BAT") ? "CALL " : "", run, run, g_tools);
    }
    n = snprintf(body, sizeof body,
        "%s%s"
        "%s\\UCDD.EXE -mount %s > %s\\CDOUT.TXT\r\n"
        "IF ERRORLEVEL 1 GOTO BAD\r\n"
        "ECHO %s> %s\\MOUNTED.FLG\r\n"
        "ECHO OK %s> %s\\CDRES.TXT\r\n",
        is_mounted(m) ? "GOTO RUN\r\n" : "", eject_old, g_tools, img, g_tools, rel, g_tools, rel, g_tools);
    snprintf(body + n, sizeof body - (size_t)n,
        ":RUN\r\n"
        "%s"
        "GOTO END\r\n"
        ":BAD\r\n"
        "ECHO BAD %s> %s\\CDRES.TXT\r\n"
        "GOTO END\r\n"
        ":STUCK\r\n"
        "ECHO STUCK %s> %s\\CDRES.TXT\r\n"
        ":END\r\n", prog, rel, g_tools, rel, g_tools);
    return write_run(body);
}

static int eject(void)
{
    char body[900];
    snprintf(body, sizeof body,
        "%s\\UCDD.EXE -unmount > %s\\CDOUT.TXT\r\n"
        "IF ERRORLEVEL 1 GOTO STUCK\r\n"
        "IF EXIST %s\\MOUNTED.FLG DEL %s\\MOUNTED.FLG\r\n"
        "ECHO EJECT> %s\\CDRES.TXT\r\n"
        "GOTO END\r\n"
        ":STUCK\r\n"
        "ECHO STUCK> %s\\CDRES.TXT\r\n"
        ":END\r\n",
        g_tools, g_tools, g_tools, g_tools, g_tools, g_tools);
    return write_run(body);
}

/* NAME.CUE -> NAME_.CUE (or NAM_.CUE ...) with the FILE line pointing to the short BIN name */
static int fix_cue(IMG* m, char* out)
{
    char src[200], dst[200], base[13], line[300];
    FILE *fi, *fo; int k, wrote = 0;
    snprintf(base, sizeof base, "%.*s", (int)strcspn(m->file, "."), m->file);
    k = (int)strlen(base); if (k > 7) k = 7;
    snprintf(out, 13, "%.*s_.CUE", k, base);
    in_cur(src, sizeof src, m->file);
    in_cur(dst, sizeof dst, out);
    if (!(fi = fopen(src, "r"))) return 0;
    if (!(fo = fopen(dst, "wb"))) { fclose(fi); return 0; }
    while (fgets(line, sizeof line, fi))
    {
        char* p = line;
        line[strcspn(line, "\r\n")] = 0;
        while (*p == ' ' || *p == '\t') p++;
        if (!wrote && !strncasecmp(p, "FILE", 4))
        {
            const char* kind = strrchr(p, ' ');
            fprintf(fo, "FILE \"%s\"%s\r\n", m->fixbin, kind && kind > p + 5 && !strchr(kind, '"') ? kind : " BINARY");
            wrote = 1;
        }
        else fprintf(fo, "%s\r\n", line);
    }
    fclose(fi); fclose(fo);
    return 1;
}

/* the result of the last mount / eject (GUI.BAT ran CDRUN.BAT and started us again) */
static void show_result(void)
{
    char path[200], res[120] = "", out[600] = "", msg[800]; FILE* f; size_t n = 0;
    path_of(path, sizeof path, g_tools, "CDRES.TXT");
    if (!(f = fopen(path, "r"))) return;
    if (!fgets(res, sizeof res, f)) res[0] = 0;
    fclose(f); remove(path);
    res[strcspn(res, "\r\n")] = 0;
    path_of(path, sizeof path, g_tools, "CDOUT.TXT");
    if ((f = fopen(path, "r")))
    {
        int c, sp = 0;
        while ((c = fgetc(f)) != EOF && n + 2 < sizeof out)
        {
            if (c == '\r' || c == '\n' || c == '\t') c = ' ';
            if (c == ' ' && (sp || !n)) continue;
            sp = c == ' '; out[n++] = (char)c;
        }
        while (n && (out[n - 1] == ' ' || out[n - 1] == '.')) n--;
        out[n] = 0; fclose(f); remove(path);
    }
    draw();
    if (!strncmp(res, "OK", 2))
    {
        if (g_label[0]) snprintf(msg, sizeof msg, "%s is in drive M: (disc label %s).", res + 3, g_label);
        else snprintf(msg, sizeof msg, "%s is in drive M: now.", res + 3);
        ui_message("Mounted", msg);
    }
    else if (!strncmp(res, "RUN", 3))
    {
        snprintf(msg, sizeof msg, "%s has finished. The disc stays in drive M: - start the game from the menu, "
                 "or its folder on drive C:.", res + 4);
        ui_message("Installer", msg);
    }
    else if (!strncmp(res, "EJECT", 5)) ui_message("Ejected", "Drive M: is empty now.");
    else if (!strncmp(res, "STUCK", 5))
    {
        snprintf(msg, sizeof msg, "uCDD could not take the disc out of drive M (%s). Close any program that uses "
                 "drive M and try again; if it still says this, restart the PC to change discs.", out[0] ? out : "no message");
        ui_message("Not ejected", msg);
    }
    else
    {
        snprintf(msg, sizeof msg, "uCDD could not mount %s. %s", res + 4, out[0] ? out : "(no message)");
        ui_message("Not mounted", msg);
    }
}

static int can_mount(const IMG* m)
{
    if (!g_cdon) { ui_message("Virtual drive off", "uCDD did not start (see the messages before this screen). "
                               "Restart the PC to plain DOS and open CD images again. CDDIAG.BAT in C:\\CDSET checks the setup."); return 0; }
    if (m->type == T_CUE && m->files > 1) { ui_message("Cannot mount", "uCDD needs a CUE with one BIN file. Join the BINs (binmerge) first."); return 0; }
    if (m->type == T_CUE && !m->bin_ok)
    {
        ui_message("Cannot mount", m->fixbin[0] ? "DOS cannot open the BIN name in this CUE. Press F to write a fixed copy, then mount that."
                                                 : "The BIN file named in this CUE is not in the folder.");
        return 0;
    }
    return 1;
}

static void go_up(void)
{
    char was[16], *s = strrchr(g_cur, '\\');
    if (strlen(g_cur) <= 2 || !s) return;
    snprintf(was, sizeof was, "%s", s + 1);
    *s = 0;
    g_sel = g_top = 0;
    scan();
    select_file(was);
}

int main(int argc, char** argv)
{
    int c, go = 0;
    char sel[16];
    putenv("LFN=n");                          /* short names only: uCDD and the CUE files need them */
    if (argc > 1 && argv[1][0]) snprintf(g_home, sizeof g_home, "%s", argv[1]);
    if (argc > 2 && argv[2][0]) snprintf(g_imgdir, sizeof g_imgdir, "%s", argv[2]);
    if (argc > 3 && argv[3][0]) snprintf(g_tools, sizeof g_tools, "%s", argv[3]);
    upcase(g_imgdir);
    snprintf(g_homedir, sizeof g_homedir, "%s", g_imgdir);
    go_dir(g_imgdir);
    { char p[200]; path_of(p, sizeof p, g_tools, "CDRUN.BAT"); remove(p); }
    cfg_load(sel, sizeof sel);
    read_state();
    /* after a mount, open the folder of the disc in the drive */
    if (g_mounted[0] && strchr(g_mounted, '.'))
    {
        char full[200], *s;
        abs_of(full, sizeof full, g_mounted);
        s = strrchr(full, '\\');
        if (s) { *s = 0; if (dir_ok(full)) { go_dir(full); snprintf(sel, sizeof sel, "%s", s + 1); } }
    }
    scan();
    select_file(sel);
    tui_theme_load(g_home);
    tui_anim = anim;
    if (f3_wanted(g_home) && f3_enter()) g3 = 1;      /* the 3D menu is on: this screen in 3D too */
    else tui_init();
    c3_build(&c3_v);
    show_result();

    while (!go)
    {
        IMG* m = g_n ? &g_img[g_sel] : NULL;
        if (g3)
        {
            c3_build(&c3_v);
            f3_frame(&c3_v);
            c = f3_key(&c3_v);
            if (!c) continue;
            if (g_n && c3_v.sel != g_sel && c3_v.sel < g_n) g_sel = c3_v.sel;   /* clicked a row */
            m = g_n ? &g_img[g_sel] : NULL;
            if (c == F3K_REDRAW || c == F3K_TABSEL) continue;
            if (c == F3K_F8) { f3_cfg_off(); f3_leave(); g3 = 0; tui_init(); continue; }
        }
        else
        {
            draw();
            c = tui_key();
            if (c == 0x142)                                                     /* F8: 3D on */
            {
                f3_cfg_load(g_home); f3_cfg_save(); tui_done();
                if (f3_enter()) g3 = 1;
                else { f3_cfg_off(); tui_init(); tui_message("3D screens", "This PC's video BIOS has no 640x480 VESA mode with 15/16/24/32-bit colour."); }
                continue;
            }
        }
        if (c == K_ESC || c == 'q' || c == 'Q') break;
        else if (c == K_UP) { if (g_sel > 0) g_sel--; }
        else if (c == K_DOWN) { if (g_sel + 1 < g_n) g_sel++; }
        else if (c == K_PGUP) { g_sel -= VIS; if (g_sel < 0) g_sel = 0; }
        else if (c == K_PGDN) { g_sel += VIS; if (g_sel >= g_n) g_sel = g_n ? g_n - 1 : 0; }
        else if (c == K_HOME) g_sel = 0;
        else if (c == K_END) g_sel = g_n ? g_n - 1 : 0;
        else if (c == 8 || c == K_LEFT) go_up();
        else if (c == 'd' || c == 'D')
        {   /* the next drive that can be read (C: and up; A:/B: are floppies) */
            int d0 = g_cur[0], k;
            for (k = 1; k <= 24; k++)
            {
                char nd[4]; snprintf(nd, sizeof nd, "%c:", 'C' + (d0 - 'C' + k) % 24);
                if (dir_ok(nd)) { go_dir(nd); g_sel = g_top = 0; scan(); break; }
            }
        }
        else if (c == 'g' || c == 'G')
        {
            if (!dir_ok(g_homedir)) ui_message("Home folder", "The home folder cannot be read (a USB drive that is not plugged in?).");
            else { go_dir(g_homedir); g_sel = g_top = 0; scan(); }
        }
        else if (c == K_F2)
        {
            char q[220];
            snprintf(g_homedir, sizeof g_homedir, "%s", g_cur);
            cfg_save();
            snprintf(q, sizeof q, "%s is now the home folder: this screen opens here when the last folder is gone, and G comes back here.", g_cur);
            ui_message("Home folder", q);
        }
        else if (c == 'r' || c == 'R') { char keep[13]; snprintf(keep, sizeof keep, "%s", m ? m->file : ""); scan(); read_state(); select_file(keep); }
        else if (c == 's' || c == 'S') { g_sort = (g_sort + 1) % S_COUNT; sort_list(); }
        else if (c == K_F1 || c == 'h' || c == 'H' || c == '?') { if (g3) { c3_text_begin(); help(); c3_text_end(); } else help(); }
        else if (c == K_F3) { tui_theme = (tui_theme + 1) % THEME_COUNT; tui_apply_theme(); tui_theme_save(g_home); tui_invalidate(); }
        else if (c == 0x13E) { draw(); tui_pick_mode(g_home); tui_invalidate(); }
        else if (c == 0x140) { tui_animate = !tui_animate; tui_theme_save(g_home); }
        else if (c == 0x141) { tui_font = (tui_font + 1) % FONT_COUNT; tui_font_apply(); tui_theme_save(g_home); }
        else if ((c == 'c' || c == 'C') && m) { if (g3 && m->type > T_DIR) { c3_text_begin(); contents(m); c3_text_end(); } else contents(m); }
        else if (c == 'u' || c == 'U')
        {
            if (!g_mounted[0]) ui_message("Eject", "There is no disc in drive M:.");
            else if (eject()) go = 1;
        }
        else if ((c == 'f' || c == 'F') && m)
        {
            char nm[13], q[200];
            if (m->type != T_CUE || m->bin_ok) ui_message("Fix CUE", "This image needs no fixing.");
            else if (m->files > 1) ui_message("Fix CUE", "This CUE uses several BIN files. Join them with binmerge first.");
            else if (!m->fixbin[0]) ui_message("Fix CUE", "The BIN this CUE names is not in the folder, so there is nothing to point it to.");
            else
            {
                snprintf(q, sizeof q, "Write a copy of %s that uses %s? The original stays as it is.", m->file, m->fixbin);
                if (ui_yesno(q))
                {
                    if (!fix_cue(m, nm)) ui_message("Fix CUE", "Could not write the new CUE file (disk full or read-only?).");
                    else
                    {
                        scan();
                        select_file(nm);
                        snprintf(q, sizeof q, "Written %s. It is selected - press Enter to mount it.", nm);
                        draw(); ui_message("Fix CUE", q);
                    }
                }
            }
        }
        else if ((c == 'i' || c == 'I') && m && m->type > T_DIR)
        {
            char q[200];
            if (!m->install[0]) ui_message("Install", m->fs_ok ? "There is no INSTALL or SETUP program in the disc's root folder. Mount it (Enter) and look on drive M:, or press C to see its files."
                                                                : "This screen cannot read the files of this image. Mount it (Enter) and look on drive M:.");
            else if (can_mount(m))
            {
                snprintf(q, sizeof q, "Mount %s and run %s from it? Install the game to drive C:. Afterwards this screen comes back.", m->file, m->install);
                if (ui_yesno(q))
                {
                    if (mount(m, m->install)) go = 1;
                    else ui_message("Install", "Could not write CDRUN.BAT in the CD tools folder.");
                }
            }
        }
        else if ((c == K_ENTER || c == K_RIGHT) && m)
        {
            if (m->type == T_UP) go_up();
            else if (m->type == T_DIR)
            {
                char d[200];
                path_of(d, sizeof d, g_cur, m->file);
                if (!dir_ok(d)) ui_message("Folder", "That folder cannot be read.");
                else { go_dir(d); g_sel = g_top = 0; scan(); }
            }
            else if (c == K_RIGHT) ;
            else if (!can_mount(m)) ;
            else if (is_mounted(m)) ui_message("Mount", "That disc is already in drive M:.");
            else if (mount(m, NULL)) go = 1;
            else ui_message("Mount", "Could not write CDRUN.BAT in the CD tools folder.");
        }
    }
    cfg_save();
    if (g3) f3_leave();
    tui_done();
    if (go) printf("Working with drive M: ...\n");
    return 0;
}
