/* padconv.c - turn DOSBox Pure's keyb2joypad.cpp (the Keyb2Joypad game pad layouts by Jemy Murphy
 * and bigjim, downloaded by the user) into PADMAP.TXT, the layout file of NUKED SB-EMU.
 *
 *   PADCONV keyb2joypad.cpp PADMAP.TXT
 *
 * The .cpp only holds data: a table of 32-bit file ids and four raw-deflate compressed blocks
 * written as C string literals. Nothing in it is compiled or run; this program reads the literals,
 * inflates them (own inflater below) and writes one section per game:
 *
 *   [Commander Keen 4 - Secret of the Oracle]
 *   Year=1991
 *   Id=B408B7B3 B408FAD3 ...          file ids: FNV-1 (multiply, then xor) over the upper-case 8.3 file name ^ (size << 3)
 *   A=LCtrl ; Jump                    pad button (Xbox names) = key[+key...] ; action
 *   LStickX=Left Right ; Move ...     axis = key for the negative side, key for the positive side
 *
 * Plain C, builds for Windows (MinGW) and DOS (DJGPP / Open Watcom 32-bit). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;

#ifdef PADCONV_LIB        /* built into SC55W.EXE: errors return to padconv_run() */
#include <setjmp.h>
static jmp_buf die_jb;
static const char* die_msg;
static void die(const char* m) { die_msg = m; longjmp(die_jb, 1); }
#else
static void die(const char* m) { fprintf(stderr, "PADCONV: %s\n", m); exit(1); }
#endif

/* ---------------------------------------------------------------- raw inflate (RFC 1951) */
typedef struct { const u8* in; long inlen, pos; u32 bitbuf; int bitcnt; u8* out; long outlen, outpos; } Inf;
typedef struct { short count[16], symbol[288]; } Huff;

static int bits(Inf* s, int need)
{
    u32 v = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->pos >= s->inlen) die("compressed data ends early");
        v |= (u32)s->in[s->pos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = v >> need;
    s->bitcnt -= need;
    return (int)(v & ((1UL << need) - 1));
}

static int decode(Inf* s, const Huff* h)
{
    int code = 0, first = 0, index = 0, len;
    for (len = 1; len < 16; len++) {
        int count;
        code |= bits(s, 1);
        count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count; first += count; first <<= 1; code <<= 1;
    }
    die("bad compressed data");
    return 0;
}

static void build(Huff* h, const short* length, int n)
{
    short offs[16];
    int i;
    memset(h->count, 0, sizeof h->count);
    for (i = 0; i < n; i++) h->count[length[i]]++;
    h->count[0] = 0;
    offs[1] = 0;
    for (i = 1; i < 15; i++) offs[i + 1] = (short)(offs[i] + h->count[i]);
    for (i = 0; i < n; i++) if (length[i]) h->symbol[offs[length[i]]++] = (short)i;
}

static void put(Inf* s, u8 c)
{
    if (s->outpos >= s->outlen) die("inflated data too long");
    s->out[s->outpos++] = c;
}

static void codes(Inf* s, const Huff* lit, const Huff* dist)
{
    static const short lbase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const short lext[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const short dbase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,
                                    4097,6145,8193,12289,16385,24577};
    static const short dext[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    for (;;) {
        int sym = decode(s, lit);
        if (sym < 256) put(s, (u8)sym);
        else if (sym == 256) return;
        else {
            long len, d;
            sym -= 257;
            if (sym >= 29) die("bad length code");
            len = lbase[sym] + bits(s, lext[sym]);
            sym = decode(s, dist);
            if (sym >= 30) die("bad distance code");
            d = dbase[sym] + bits(s, dext[sym]);
            if (d > s->outpos) die("bad distance");
            while (len--) put(s, s->out[s->outpos - d]);
        }
    }
}

static void inflate_raw(const u8* in, long inlen, u8* out, long outlen)
{
    Inf s;
    int last;
    memset(&s, 0, sizeof s);
    s.in = in; s.inlen = inlen; s.out = out; s.outlen = outlen;
    do {
        int type;
        last = bits(&s, 1);
        type = bits(&s, 2);
        if (type == 0) {                       /* stored */
            long n;
            s.bitbuf = 0; s.bitcnt = 0;
            if (s.pos + 4 > s.inlen) die("compressed data ends early");
            n = s.in[s.pos] | (s.in[s.pos + 1] << 8);
            s.pos += 4;
            if (s.pos + n > s.inlen) die("compressed data ends early");
            while (n--) put(&s, s.in[s.pos++]);
        } else if (type == 1 || type == 2) {
            static Huff lit, dist;
            short len[320];
            int i;
            if (type == 1) {                   /* fixed codes */
                for (i = 0; i < 144; i++) len[i] = 8;
                for (; i < 256; i++) len[i] = 9;
                for (; i < 280; i++) len[i] = 7;
                for (; i < 288; i++) len[i] = 8;
                build(&lit, len, 288);
                for (i = 0; i < 30; i++) len[i] = 5;
                build(&dist, len, 30);
            } else {                           /* dynamic codes */
                static const short order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
                int nlen = bits(&s, 5) + 257, ndist = bits(&s, 5) + 1, ncode = bits(&s, 4) + 4, idx = 0;
                Huff lencode;
                for (i = 0; i < 19; i++) len[order[i]] = (short)(i < ncode ? bits(&s, 3) : 0);
                build(&lencode, len, 19);
                while (idx < nlen + ndist) {
                    int sym = decode(&s, &lencode), rep, val = 0;
                    if (sym < 16) { len[idx++] = (short)sym; continue; }
                    if (sym == 16) { if (!idx) die("bad code lengths"); val = len[idx - 1]; rep = 3 + bits(&s, 2); }
                    else if (sym == 17) rep = 3 + bits(&s, 3);
                    else rep = 11 + bits(&s, 7);
                    if (idx + rep > nlen + ndist) die("bad code lengths");
                    while (rep--) len[idx++] = (short)val;
                }
                build(&lit, len, nlen);
                build(&dist, len + nlen, ndist);
            }
            codes(&s, &lit, &dist);
        } else die("bad block type");
    } while (!last);
    if (s.outpos != outlen) die("inflated size does not match");
}

/* ---------------------------------------------------------------- reading the .cpp */
static char* src;
static long srclen;

/* the C string literals starting at p (adjacent ones joined); returns bytes, sets *n and *end */
static u8* literals(const char* p, long* n, const char** end)
{
    u8* out = (u8*)malloc(strlen(p) + 1);
    long o = 0;
    if (!out) die("out of memory");
    for (;;) {
        while (*p && isspace((u8)*p)) p++;
        if (*p != '"') break;
        p++;
        while (*p && *p != '"') {
            if (*p != '\\') { out[o++] = (u8)*p++; continue; }
            p++;
            if (*p >= '0' && *p <= '7') {
                int v = 0, k = 0;
                while (k < 3 && *p >= '0' && *p <= '7') { v = v * 8 + (*p++ - '0'); k++; }
                out[o++] = (u8)v;
            } else if (*p == 'x') {
                int v = 0;
                p++;
                while (isxdigit((u8)*p)) { v = v * 16 + (isdigit((u8)*p) ? *p - '0' : (tolower((u8)*p) - 'a' + 10)); p++; }
                out[o++] = (u8)v;
            } else {
                switch (*p) {
                case 'n': out[o++] = 10; break; case 't': out[o++] = 9; break; case 'r': out[o++] = 13; break;
                case 'a': out[o++] = 7; break; case 'b': out[o++] = 8; break; case 'f': out[o++] = 12; break;
                case 'v': out[o++] = 11; break; default: out[o++] = (u8)*p; break;
                }
                p++;
            }
        }
        if (*p == '"') p++;
    }
    *n = o; *end = p;
    return out;
}

static u32 number(const char** pp)
{
    const char* p = *pp;
    u32 v = 0;
    while (*p && !isdigit((u8)*p)) p++;
    if (!*p) die("unexpected end of keyb2joypad.cpp");
    while (isdigit((u8)*p)) v = v * 10 + (u32)(*p++ - '0');
    *pp = p;
    return v;
}

#define NB 4
static long table;
static u32* keys;
static struct { u8 *ids, *maps; u32 act; } bk[NB];

static void parse(void)
{
    const char *p = strstr(src, "map_keys[MAP_TABLE_SIZE] ="), *e, *q;
    long i;
    int b;
    if (!p) die("this is not keyb2joypad.cpp (no map_keys table)");
    p = strchr(p, '{');
    e = p ? strchr(p, '}') : NULL;
    if (!e) die("map_keys table not complete");
    for (table = 0, q = p; q < e; q++)       /* the table size is in the .h: count the entries */
        if (isdigit((u8)*q) && !isdigit((u8)q[-1])) table++;
    if (table < 16 || table > 1000000L || table % NB) die("unexpected table size");
    keys = (u32*)malloc(table * sizeof(u32));
    if (!keys) die("out of memory");
    for (i = 0; i < table; i++) keys[i] = number(&p);
    p = strstr(p, "map_buckets[MAP_BUCKETS] =");
    if (!p) die("map_buckets not found");
    for (b = 0; b < NB; b++) {
        long ln, mn;
        u32 ic, iu, mc, mu;
        const char* q;
        u8 *idc, *mpc;
        p = strchr(p, '"');
        if (!p) die("bucket data not found");
        idc = literals(p, &ln, &q); p = q;
        ic = number(&p); iu = number(&p);
        p = strchr(p, '"');
        if (!p) die("bucket data not found");
        mpc = literals(p, &mn, &q); p = q;
        mc = number(&p); mu = number(&p); bk[b].act = number(&p);
        if ((u32)ln != ic || (u32)mn != mc) die("bucket sizes do not match");
        bk[b].ids = (u8*)malloc(iu + 1); bk[b].maps = (u8*)malloc(mu + 1);
        if (!bk[b].ids || !bk[b].maps) die("out of memory");
        inflate_raw(idc, (long)ic, bk[b].ids, (long)iu); bk[b].ids[iu] = 0;
        inflate_raw(mpc, (long)mc, bk[b].maps, (long)mu); bk[b].maps[mu] = 0;
        free(idc); free(mpc);
    }
}

/* ---------------------------------------------------------------- names */
/* pad buttons as RetroArch numbers them (SNES positions) -> Xbox names */
static const char* const BTN[20] = {"A", "X", "Back", "Start", "DUp", "DDown", "DLeft", "DRight", "B", "Y",
                                    "LB", "RB", "LT", "RT", "LS", "RS", "LStickX", "LStickY", "RStickX", "RStickY"};
static const char* const KBD[] = {"None", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "A", "S", "D", "F", "G", "H", "J", "K", "L", "Z",
    "X", "C", "V", "B", "N", "M", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
    "Esc", "Tab", "Backspace", "Enter", "Space", "LAlt", "RAlt", "LCtrl", "RCtrl", "LShift", "RShift",
    "CapsLock", "ScrollLock", "NumLock", "Grave", "Minus", "Equals", "Backslash", "LBracket", "RBracket",
    "Semicolon", "Quote", "Period", "Comma", "Slash", "LessGreater", "PrintScreen", "Pause",
    "Insert", "Home", "PageUp", "Delete", "End", "PageDown", "Left", "Up", "Down", "Right",
    "KP1", "KP2", "KP3", "KP4", "KP5", "KP6", "KP7", "KP8", "KP9", "KP0",
    "KPSlash", "KPStar", "KPMinus", "KPPlus", "KPEnter", "KPPeriod"};
static const char* const SPEC[] = {"MouseUp", "MouseDown", "MouseLeft", "MouseRight", "MouseButton1",
    "MouseButton2", "MouseButton3", "MouseFaster", "MouseSlower", "JoyUp", "JoyDown", "JoyLeft", "JoyRight",
    "JoyButton1", "JoyButton2", "JoyButton3", "JoyButton4", "HatUp", "HatDown", "HatLeft", "HatRight",
    "Joy2Up", "Joy2Down", "Joy2Left", "Joy2Right", "Keyboard"};
#define NKBD (int)(sizeof KBD / sizeof KBD[0])
#define NSPEC (int)(sizeof SPEC / sizeof SPEC[0])

static const char* kname(int k)
{
    static char b[8];
    if (k < NKBD) return KBD[k];
    if (k >= 200 && k < 200 + NSPEC) return SPEC[k - 200];
    sprintf(b, "?%d", k);
    return b;
}

/* ---------------------------------------------------------------- output */
typedef struct { int mb; unsigned moff; const char* title; int year; long first; } Game;

static int cmp_title(const void* a, const void* b)
{
    const Game *x = (const Game*)a, *y = (const Game*)b;
    const char *p = x->title, *q = y->title;
    for (; *p && tolower((u8)*p) == tolower((u8)*q); p++, q++) ;
    if (tolower((u8)*p) != tolower((u8)*q)) return tolower((u8)*p) - tolower((u8)*q);
    return x->year != y->year ? x->year - y->year : (x->first < y->first ? -1 : 1);
}

/* titles that differ only in case ("OverKill" 1992, "Overkill" 1996) are different games too:
   SC55W matches section titles without case, so they get numbered like exact repeats */
static int same_title(const char* p, const char* q)
{
    for (; *p && tolower((u8)*p) == tolower((u8)*q); p++, q++) ;
    return tolower((u8)*p) == tolower((u8)*q);
}

static long convert(const char* in, const char* outname, long* nid_out);
static void free_all(void);

#ifdef PADCONV_LIB
int padconv_run(const char* in, const char* out, char* msg, int msglen)
{
    long ng, nid = 0;
    if (setjmp(die_jb)) {
        free_all();
        snprintf(msg, msglen, "%s", die_msg);
        return 1;
    }
    ng = convert(in, out, &nid);
    free_all();
    snprintf(msg, msglen, "%ld games, %ld file ids", ng, nid);
    return 0;
}
#else
int main(int argc, char** argv)
{
    long ng, nid = 0;
    if (argc != 3) {
        fprintf(stderr, "PADCONV keyb2joypad.cpp PADMAP.TXT\n"
                        "  turns DOSBox Pure's Keyb2Joypad game pad layouts into NUKED SB-EMU's PADMAP.TXT\n");
        return 2;
    }
    ng = convert(argv[1], argv[2], &nid);
    free_all();
    printf("PADCONV: %ld games, %ld file ids -> %s\n", ng, nid, argv[2]);
    return 0;
}
#endif

static Game* g;
static void free_all(void)
{
    int b;
    free(src); src = NULL;
    free(keys); keys = NULL;
    free(g); g = NULL;
    for (b = 0; b < NB; b++) { free(bk[b].ids); free(bk[b].maps); bk[b].ids = bk[b].maps = NULL; }
}

static long convert(const char* in, const char* outname, long* nid_out)
{
    FILE* f;
    long i, ng = 0, nid = 0;
    f = fopen(in, "rb");
    if (!f) die("cannot open the input file");
    fseek(f, 0, SEEK_END); srclen = ftell(f); fseek(f, 0, SEEK_SET);
    src = (char*)malloc(srclen + 1);
    if (!src || fread(src, 1, srclen, f) != (size_t)srclen) die("cannot read the input file");
    src[srclen] = 0;
    fclose(f);
    parse();

    /* one entry per (title, year, mapping); ids collected in table order */
    g = (Game*)malloc(table * sizeof(Game));
    if (!g) die("out of memory");
    for (i = 0; i < table; i++) {
        const u8 *id, *t;
        long j;
        int mb;
        unsigned moff;
        if (!keys[i]) continue;
        nid++;
        id = bk[i % NB].ids + (i / NB) * 5;
        mb = id[0] % NB; moff = (id[1] << 8) | id[2];
        t = bk[i % NB].ids + (table / NB) * 5 + ((id[3] << 8) | id[4]);
        for (j = 0; j < ng; j++)
            if (g[j].mb == mb && g[j].moff == moff && g[j].year == 1970 + t[0] && !strcmp(g[j].title, (const char*)t + 1)) break;
        if (j == ng) { g[ng].mb = mb; g[ng].moff = moff; g[ng].title = (const char*)t + 1; g[ng].year = 1970 + t[0]; g[ng].first = i; ng++; }
    }
    qsort(g, ng, sizeof *g, cmp_title);

    f = fopen(outname, "w");
    if (!f) die("cannot create the output file");
    fprintf(f, "; PADMAP.TXT - game pad layouts for NUKED SB-EMU, %ld games.\n"
               "; Made by PADCONV from keyb2joypad.cpp: the Keyb2Joypad project by Jemy Murphy and bigjim,\n"
               "; as built into DOSBox Pure. Do not edit: put your own layouts in PADUSER.TXT (same format).\n"
               ";   Id=      files of the game: FNV-1 of the upper-case file name, xor (file size << 3)\n"
               ";   Button=  key[+key...]  ; action     Axis= key-for-left/up key-for-right/down\n"
               ";   Buttons: A B X Y LB RB LT RT LS RS Back Start DUp DDown DLeft DRight\n"
               ";   Axes:    LStickX LStickY RStickX RStickY\n", ng);
    for (i = 0; i < ng; i++) {
        const u8 *ms = bk[g[i].mb].maps, *p = ms + g[i].moff;
        int cnt = *p++, n, dup = 0;
        long j;
        for (j = i - 1; j >= 0 && same_title(g[j].title, g[i].title); j--) dup++;
        fprintf(f, "\n[%s", g[i].title);
        if (dup) fprintf(f, " (%d)", dup + 1);
        fprintf(f, "]\nYear=%d\nId=", g[i].year);
        for (j = 0, n = 0; j < table; j++) {
            const u8* id;
            if (!keys[j]) continue;
            id = bk[j % NB].ids + (j / NB) * 5;
            if (id[0] % NB == g[i].mb && (unsigned)((id[1] << 8) | id[2]) == g[i].moff &&
                !strcmp((const char*)bk[j % NB].ids + (table / NB) * 5 + ((id[3] << 8) | id[4]) + 1, g[i].title))
                fprintf(f, "%s%08lX", n++ ? " " : "", (unsigned long)keys[j]);
        }
        fputc('\n', f);
        while (cnt--) {
            int v = *p++, id = v & 31, analog = id >= 16, named = v & 32, kc = 1 + (v >> 6), k;
            const char* action = NULL;
            if (id > 19) die("unexpected button number in the data");
            if (named) {
                u32 no = 0;
                do no = (no << 7) | (*p & 127); while (*p++ & 128);
                action = (const char*)ms + bk[g[i].mb].act + no;
            }
            fprintf(f, "%s=", BTN[id]);
            if (analog) {
                for (k = 0; k < kc; k++) fprintf(f, "%s%s", k ? "+" : "", kname(p[k * 2]));
                fputc(' ', f);
                for (k = 0; k < kc; k++) fprintf(f, "%s%s", k ? "+" : "", kname(p[k * 2 + 1]));
                p += kc * 2;
            } else {
                for (k = 0; k < kc; k++) fprintf(f, "%s%s", k ? "+" : "", kname(p[k]));
                p += kc;
            }
            if (action && *action) fprintf(f, " ; %s", action);
            fputc('\n', f);
        }
    }
    fclose(f);
    *nid_out = nid;
    return ng;
}
