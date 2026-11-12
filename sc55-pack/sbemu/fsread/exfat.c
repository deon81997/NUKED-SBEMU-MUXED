/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* fsread: exFAT reader + the parts shared with ntfs.c (names, patterns, dispatch).
 * See fsread.h. Read-only, C99, no libc beyond memcpy/memcmp/memset. */
#include <string.h>
#include "fsread.h"

#define LE16 fsr_le16
#define LE32 fsr_le32
#define LE64 fsr_le64
#define NOTAG (~(uint64_t)0)

/* ---------------------------------------------------------------- shared */

int fsr_sread(fsr_vol *v, uint64_t lba)
{
    if (v->sec_tag == lba) return 0;
    v->sec_tag = NOTAG;
    if (v->rd(v->ctx, lba, 1, v->sec)) return FSR_EIO;
    v->sec_tag = lba;
    return 0;
}

static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

static int ok83(unsigned c)   /* ASCII char allowed in a DOS 8.3 name (upper-cased) */
{
    const char *p = "\"*+,./:;<=>?[\\]|";
    if (c <= 0x20 || c >= 0x7F) return 0;
    for (; *p; p++) if ((unsigned)*p == c) return 0;
    return 1;
}
static unsigned upa(unsigned c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

int fsr_valid83(fsr_vol *v, char *out)
{
    int i, n = v->nmlen, dot = -1, o = 0;
    for (i = 0; i < n; i++) if (v->nm[i] == '.') { if (dot >= 0) return 0; dot = i; }
    if (dot == 0 || n == 0 || dot == n - 1) return 0;
    if ((dot < 0 ? n : dot) > 8 || (dot >= 0 && n - dot - 1 > 3)) return 0;
    for (i = 0; i < n; i++) {
        unsigned c = v->nm[i];
        if (i != dot && !ok83(upa(c))) return 0;
        if (out) out[o++] = (char)upa(c);
    }
    if (out) out[o] = 0;
    return 1;
}

void fsr_aliaskey(fsr_vol *v, char key[10])
{
    int i, n = v->nmlen, dot = -1, b = 0, e = 0;
    memset(key, ' ', 9); key[9] = 0;
    for (i = n - 1; i > 0; i--) if (v->nm[i] == '.') { dot = i; break; }
    for (i = 0; i < n; i++) {
        unsigned c = v->nm[i];
        if (i == dot) continue;
        if (c == ' ' || c == '.') continue;
        c = upa(c);
        if (!ok83(c)) c = '_';
        if (dot < 0 || i < dot) { if (b < 6) key[b++] = (char)c; }
        else if (e < 3) key[6 + e++] = (char)c;
    }
    if (!b) key[0] = '_';
}

void fsr_alias(const char key[10], uint32_t n, char out[13])
{
    char dg[11]; int nd = 0, i, o = 0, keep;
    do { dg[nd++] = (char)('0' + n % 10); n /= 10; } while (n);
    keep = 7 - nd; if (keep > 6) keep = 6; if (keep < 1) keep = 1;
    for (i = 0; i < keep && key[i] != ' '; i++) out[o++] = key[i];
    out[o++] = '~';
    while (nd) out[o++] = dg[--nd];
    if (key[6] != ' ') { out[o++] = '.'; for (i = 6; i < 9 && key[i] != ' '; i++) out[o++] = key[i]; }
    out[o] = 0;
}

uint32_t fsr_tilde(const char *s)
{
    const char *t = 0; uint32_t n = 0;
    for (; *s && *s != '.'; s++) if (*s == '~') t = s;
    if (!t || !t[1] || t[1] == '.') return 0;
    for (t++; *t && *t != '.'; t++) { if (*t < '0' || *t > '9') return 0; n = n * 10 + (uint32_t)(*t - '0'); }
    return n;
}

void fsr_mkname(fsr_vol *v, fsr_dirent *d)
{
    int i, o = 0;
    for (i = 0; i < v->nmlen; i++) {
        uint32_t c = v->nm[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < v->nmlen && v->nm[i+1] >= 0xDC00 && v->nm[i+1] < 0xE000)
            c = 0x10000 + ((c - 0xD800) << 10) + (v->nm[++i] - 0xDC00);
        if (c < 0x80) { if (o + 1 > 255) break; d->lname[o++] = (char)c; }
        else if (c < 0x800) { if (o + 2 > 255) break; d->lname[o++] = (char)(0xC0 | c >> 6); d->lname[o++] = (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { if (o + 3 > 255) break; d->lname[o++] = (char)(0xE0 | c >> 12);
            d->lname[o++] = (char)(0x80 | (c >> 6 & 63)); d->lname[o++] = (char)(0x80 | (c & 63)); }
        else { if (o + 4 > 255) break; d->lname[o++] = (char)(0xF0 | c >> 18); d->lname[o++] = (char)(0x80 | (c >> 12 & 63));
            d->lname[o++] = (char)(0x80 | (c >> 6 & 63)); d->lname[o++] = (char)(0x80 | (c & 63)); }
    }
    d->lname[o] = 0;
}

int fsr_namecmp(fsr_vol *v, const uint16_t *a, int al, const uint16_t *b, int bl)
{
    int i;
    for (i = 0; i < al && i < bl; i++) {
        unsigned x = a[i] < 256 ? v->up[a[i]] : a[i], y = b[i] < 256 ? v->up[b[i]] : b[i];
        if (x != y) return x < y ? -1 : 1;
    }
    return al == bl ? 0 : (al < bl ? -1 : 1);
}

static void tofcb(const char *s, char f[11])
{
    int i = 0;
    memset(f, ' ', 11);
    while (*s && *s != '.') { if (i < 8) f[i++] = *s; s++; }
    if (*s == '.') { s++; for (i = 8; *s && i < 11; s++) f[i++] = *s; }
}

void fsr_pattern(const char *s, char f[11])
{
    int i = 0;
    memset(f, ' ', 11);
    for (; *s && *s != '.'; s++) {
        if (*s == '*') { while (i < 8) f[i++] = '?'; }
        else if (i < 8) f[i++] = (char)upa((unsigned char)*s);
    }
    if (*s == '.') for (s++, i = 8; *s; s++) {
        if (*s == '*') { while (i < 11) f[i++] = '?'; }
        else if (i < 11) f[i++] = (char)upa((unsigned char)*s);
    }
}

int fsr_match(const char *pat, const char *name)
{
    char f[11]; int i;
    tofcb(name, f);
    for (i = 0; i < 11; i++) if (pat[i] != '?' && pat[i] != f[i]) return 0;
    return 1;
}

int fsr_wantattr(uint8_t amask, uint8_t attr)
{
    return !(attr & (FSR_ATTR_HID | FSR_ATTR_SYS | FSR_ATTR_DIR) & ~amask);
}

/* next path component -> c[] (as UTF-16 code units) and an upper-case 8.3 copy.
 * Returns 1, 0 at end, FSR_ENOPATH if too long. */
int fsr_comp(const char **pp, uint16_t *c, int *cl, char n83[13])
{
    const char *p = *pp; int n = 0;
    while (*p == '\\' || *p == '/') p++;
    if (!*p) { *pp = p; return 0; }
    while (*p && *p != '\\' && *p != '/') {
        if (n >= 255) return FSR_ENOPATH;
        c[n] = (unsigned char)*p;
        if (n < 12) n83[n] = (char)upa((unsigned char)*p);
        n++; p++;
    }
    n83[n < 12 ? n : 12] = 0;
    if (n > 12) n83[0] = 0;
    *cl = n; *pp = p;
    return 1;
}

int fsr_mount(fsr_vol *v, fsr_read_fn rd, void *ctx, uint64_t part_lba, void *work, uint32_t ws)
{
    uint8_t *b = (uint8_t *)work; int i;
    memset(v, 0, sizeof *v);
    v->rd = rd; v->ctx = ctx; v->base = part_lba;
    v->sec_tag = v->sec2_tag = v->reca_tag = v->recb_tag = v->idx_tag = NOTAG;
    for (i = 0; i < 256; i++) v->up[i] = (uint16_t)upa((unsigned)i);
    if (ws < 4096) return FSR_ENOMEM;
    if (rd(ctx, part_lba, 1, b)) return FSR_EIO;
    if (b[510] != 0x55 || b[511] != 0xAA) return FSR_EBADFS;
    if (!memcmp(b + 3, "EXFAT   ", 8)) { v->type = FSR_EXFAT; return fsr_x_mount(v, b, ws); }
    if (!memcmp(b + 3, "NTFS    ", 8)) { v->type = FSR_NTFS; return fsr_n_mount(v, b, ws); }
    return FSR_EBADFS;
}

int fsr_open(fsr_vol *v, const char *path, fsr_file *f, fsr_dirent *info)
{
    fsr_dirent tmp;
    memset(f, 0, sizeof *f);
    if (!info) info = &tmp;
    memset(info, 0, sizeof *info);
    f->type = v->type;
    return v->type == FSR_EXFAT ? fsr_x_open(v, path, f, info) : fsr_n_open(v, path, f, info);
}

int32_t fsr_read(fsr_vol *v, fsr_file *f, void *buf, uint32_t len)
{
    int32_t r;
    if (f->attr & FSR_ATTR_DIR) return FSR_EACCES;
    if (f->pos >= f->size || !len) return 0;
    if (len > f->size - f->pos) len = (uint32_t)(f->size - f->pos);
    if (len > 0x7FFFFFFF) len = 0x7FFFFFFF;
    r = v->type == FSR_EXFAT ? fsr_x_read(v, f, buf, len) : fsr_n_read(v, f, buf, len);
    if (r > 0) f->pos += (uint32_t)r;
    return r;
}

int fsr_findfirst(fsr_vol *v, const char *dir, const char *fcb, uint8_t amask, fsr_find *s, fsr_dirent *d)
{
    memset(s, 0, sizeof *s);
    if (fcb) memcpy(s->pat, fcb, 11); else memset(s->pat, '?', 11);
    s->amask = amask;
    return v->type == FSR_EXFAT ? fsr_x_find(v, dir, s, d, 1) : fsr_n_find(v, dir, s, d, 1);
}

int fsr_findnext(fsr_vol *v, fsr_find *s, fsr_dirent *d)
{
    return v->type == FSR_EXFAT ? fsr_x_find(v, 0, s, d, 0) : fsr_n_find(v, 0, s, d, 0);
}

int fsr_freeclusters(fsr_vol *v, uint64_t *nfree)
{
    return v->type == FSR_EXFAT ? fsr_x_free(v, nfree) : fsr_n_free(v, nfree);
}

/* open by generated alias in one pass: call for every entry that would get an alias
 * (v->nm = its name), in directory order, with a zeroed t. 1 = this is NAME~tn,
 * 0 = not, -1 = too many look-alike keys (caller checks with its aliasnum). */
int fsr_tilde_hit(fsr_vol *v, fsr_tscan *t, const char *n83, uint32_t tn)
{
    char key[10], al[13]; int i;
    fsr_aliaskey(v, key); fsr_alias(key, tn, al);
    if (!streq(al, n83)) return 0;
    for (i = 0; i < t->k && memcmp(t->key[i], key, 9); i++) ;
    if (i == t->k) { if (i == 4) return -1; memcpy(t->key[i], key, 9); t->n[i] = 0; t->k++; }
    return ++t->n[i] == tn;
}

fsr_acache *fsr_acget(fsr_vol *v, uint64_t dir, const char *key, uint32_t pos)
{
    fsr_acache *b = 0; int i;
    for (i = 0; i < FSR_ACACHE; i++) {
        fsr_acache *c = &v->ac[i];
        if (c->used && c->dir == dir && c->pos <= pos && !memcmp(c->key, key, 9) && (!b || c->pos > b->pos)) b = c;
    }
    return b;
}

void fsr_acput(fsr_vol *v, uint64_t dir, const char *key, uint32_t pos, uint32_t n, const fsr_find *st)
{
    fsr_acache *c = 0; int i;
    for (i = 0; i < FSR_ACACHE; i++)
        if (v->ac[i].used && v->ac[i].dir == dir && !memcmp(v->ac[i].key, key, 9)) c = &v->ac[i];
    if (!c) { c = &v->ac[v->acn]; v->acn = (v->acn + 1) % FSR_ACACHE; }   /* else: move it (latest use wins) */
    c->used = 1; c->dir = dir; c->pos = pos; c->n = n; memcpy(c->key, key, 9); c->st = *st;
}

uint64_t fsr_popfree(const uint8_t *b, uint32_t n, uint64_t first, uint64_t nclus)
{
    uint64_t c = 0, i;
    for (i = 0; i < (uint64_t)n * 8 && first + i < nclus; i++)
        if (!(b[i >> 3] >> (i & 7) & 1)) c++;
    return c;
}

/* ----------------------------------------------------------------- exFAT */

static int x_fat(fsr_vol *v, uint32_t c, uint32_t *nx)
{
    uint64_t lba = v->fat_lba + (c >> (v->ss_shift - 2));
    if (v->sec2_tag != lba) {
        v->sec2_tag = NOTAG;
        if (v->rd(v->ctx, lba, 1, v->sec2)) return FSR_EIO;
        v->sec2_tag = lba;
    }
    *nx = LE32(v->sec2 + (c & ((v->ss >> 2) - 1)) * 4);
    return 0;
}

static int okclus(fsr_vol *v, uint32_t c) { return c >= 2 && c - 2 < v->nclus; }

/* byte offset in stream -> lba, sectors contiguous from there. 1 = end of chain. */
static int x_map(fsr_vol *v, fsr_xstream *s, uint64_t off, uint64_t *lba, uint32_t *ns)
{
    uint32_t ci = (uint32_t)(off >> v->cs_shift), sh = v->cs_shift - v->ss_shift;
    uint32_t so = (uint32_t)((off & (v->cs - 1)) >> v->ss_shift);
    uint64_t n = ((uint64_t)1 << sh) - so;
    if (!s->first) return 1;
    if (s->nofat) {
        uint64_t tot = (s->size + v->cs - 1) >> v->cs_shift;
        if (ci >= tot || !okclus(v, s->first + ci)) return ci >= tot ? 1 : FSR_EBADFS;
        s->clus = s->first + ci; s->cidx = ci;
        n = ((tot - ci) << sh) - so;
    } else {
        if (!s->clus || ci < s->cidx) { s->cidx = 0; s->clus = s->first; }
        if (ci >= v->nclus) return FSR_EBADFS;   /* also stops a looping chain */
        while (s->cidx < ci) {
            uint32_t nx; int r = x_fat(v, s->clus, &nx);
            if (r) return r;
            if (nx >= 0xFFFFFFF8u) return 1;
            if (!okclus(v, nx)) return FSR_EBADFS;
            s->clus = nx; s->cidx++;
        }
        if (!okclus(v, s->clus)) return FSR_EBADFS;
        {   /* chain runs on into adjacent clusters: read them in one go (up to 256 sectors) */
            uint32_t c = s->clus, nx;
            while (n < 256 && !x_fat(v, c, &nx) && nx == c + 1 && okclus(v, nx)) { n += (uint64_t)1 << sh; c = nx; }
        }
    }
    *lba = v->heap_lba + ((uint64_t)(s->clus - 2) << sh) + so;
    *ns = n > 0x4000 ? 0x4000 : (uint32_t)n;
    return 0;
}

static int32_t x_sread(fsr_vol *v, fsr_xstream *s, uint64_t pos, void *buf, uint32_t len)
{
    uint8_t *d = (uint8_t *)buf; uint32_t done = 0;
    if (s->size != NOTAG) { if (pos >= s->size) return 0; if (len > s->size - pos) len = (uint32_t)(s->size - pos); }
    while (done < len) {
        uint32_t n = len - done, ns, so; uint64_t lba; int r;
        if (pos >= s->vdl) { memset(d, 0, n); done += n; break; }
        if (n > s->vdl - pos) n = (uint32_t)(s->vdl - pos);
        r = x_map(v, s, pos, &lba, &ns);
        if (r == 1) break;
        if (r) return r;
        so = (uint32_t)(pos & (v->ss - 1));
        if (!so && n >= v->ss) {
            uint32_t cnt = n >> v->ss_shift;
            if (cnt > ns) cnt = ns;
            if (v->rd(v->ctx, lba, (int)cnt, d)) return FSR_EIO;
            n = cnt << v->ss_shift;
        } else {
            if ((r = fsr_sread(v, lba))) return r;
            if (n > v->ss - so) n = v->ss - so;
            memcpy(d, v->sec + so, n);
        }
        d += n; pos += n; done += n;
    }
    return (int32_t)done;
}

typedef struct { uint8_t attr; uint32_t ts, start; uint16_t hash; fsr_xstream s; } xent;

/* entry set at *idx -> e + v->nm; *idx advanced. 1 found, 0 end, < 0 error */
static int x_next(fsr_vol *v, fsr_xstream *dir, uint32_t *idx, xent *e)
{
    uint8_t b[32];
    for (;;) {
        uint32_t i0 = *idx; int sc, k, r, nl;
        r = x_sread(v, dir, (uint64_t)i0 * 32, b, 32);
        if (r < 0) return r;
        if (r < 32 || !b[0]) return 0;
        (*idx)++;
        if (b[0] != 0x85 || b[1] < 2) continue;
        sc = b[1]; e->start = i0;
        e->attr = (uint8_t)(LE16(b + 4) & 0x37); e->ts = LE32(b + 12);
        if ((r = x_sread(v, dir, (uint64_t)(i0 + 1) * 32, b, 32)) < 0) return r;
        if (r < 32 || b[0] != 0xC0) continue;
        nl = b[3]; e->hash = LE16(b + 4);
        memset(&e->s, 0, sizeof e->s);
        e->s.nofat = (b[1] & 2) != 0; e->s.vdl = LE64(b + 8);
        e->s.first = LE32(b + 20); e->s.size = LE64(b + 24);
        if (e->s.vdl > e->s.size) e->s.vdl = e->s.size;
        if (e->s.size > v->nclus << v->cs_shift) { *idx = i0 + 1 + sc; continue; }   /* corrupt: bigger than the volume */
        if (!(b[1] & 1)) e->s.first = 0;
        v->nmlen = 0;
        for (k = 2; k <= sc && v->nmlen < nl; k++) {
            int j;
            if ((r = x_sread(v, dir, (uint64_t)(i0 + k) * 32, b, 32)) < 0) return r;
            if (r < 32 || b[0] != 0xC1) break;
            for (j = 0; j < 15 && v->nmlen < nl; j++) v->nm[v->nmlen++] = LE16(b + 2 + 2 * j);
        }
        *idx = i0 + 1 + sc;
        if (!v->nmlen) continue;
        return 1;
    }
}

/* alias number of the entry at index `upto` with basis key (counts earlier ones) */
static int x_aliasnum(fsr_vol *v, const fsr_xstream *dir, uint32_t upto, const char *key, uint32_t *n)
{
    fsr_find st; uint32_t cnt = 1; xent e; char k2[10]; int r;
    fsr_acache *c = fsr_acget(v, dir->first, key, upto);
    if (c && c->pos == upto) { *n = c->n; return 0; }
    if (c) { st = c->st; cnt = c->n + 1; }
    else { st.u.x.d = *dir; st.u.x.d.clus = 0; st.u.x.idx = 0; }
    for (;;) {
        if ((r = x_next(v, &st.u.x.d, &st.u.x.idx, &e)) <= 0) { *n = cnt; return r; }
        if (e.start >= upto) {
            *n = cnt;
            if (e.start == upto) fsr_acput(v, dir->first, key, upto, cnt, &st);
            return 0;
        }
        if (fsr_valid83(v, 0)) continue;
        fsr_aliaskey(v, k2);
        if (!memcmp(k2, key, 9)) cnt++;
    }
}

static void x_fill(xent *e, fsr_dirent *d)
{
    d->attr = e->attr; d->time = (uint16_t)e->ts; d->date = (uint16_t)(e->ts >> 16);
    d->size = (e->attr & FSR_ATTR_DIR) ? 0 : e->s.size;
}

/* 8.3 name of the entry whose set started at index `start` (v->nm = its long name) */
static int x_name83(fsr_vol *v, const fsr_xstream *dir, uint32_t start, char out[13])
{
    char key[10]; uint32_t n; int r;
    if (fsr_valid83(v, out)) return 0;
    fsr_aliaskey(v, key);
    if ((r = x_aliasnum(v, dir, start, key, &n)) < 0) return r;
    fsr_alias(key, n, out);
    return 0;
}

static uint16_t x_hash(fsr_vol *v, const uint16_t *c, int n)
{
    uint16_t h = 0; int i;
    for (i = 0; i < n; i++) {
        uint16_t u = c[i] < 256 ? v->up[c[i]] : c[i];
        h = (uint16_t)(((h & 1) ? 0x8000 : 0) + (h >> 1) + (u & 0xFF));
        h = (uint16_t)(((h & 1) ? 0x8000 : 0) + (h >> 1) + (u >> 8));
    }
    return h;
}

static void x_root(fsr_vol *v, fsr_xstream *s)
{
    memset(s, 0, sizeof *s);
    s->first = v->root; s->size = s->vdl = NOTAG;
}

int fsr_x_mount(fsr_vol *v, uint8_t *b, uint32_t ws)
{
    fsr_xstream r, up; uint32_t i = 0, upclus = 0; uint64_t uplen = 0; uint8_t e[32];
    v->ss_shift = b[108]; v->cs_shift = b[108] + b[109];
    if (v->ss_shift < 9 || v->ss_shift > 12 || v->cs_shift > 25) return FSR_EBADFS;
    v->ss = 1u << v->ss_shift; v->cs = 1u << v->cs_shift;
    if (ws < 2 * v->ss) return FSR_ENOMEM;
    v->fat_lba = (uint32_t)(v->base + LE32(b + 80));
    if (b[110] == 2 && (LE16(b + 106) & 1)) v->fat_lba += LE32(b + 84);
    v->heap_lba = (uint32_t)(v->base + LE32(b + 88));
    v->nclus = LE32(b + 92); v->root = LE32(b + 96);
    v->dirty = (LE16(b + 106) & 2) != 0;
    v->sec = b; v->sec2 = b + v->ss; v->sec_tag = v->sec2_tag = NOTAG;
    if (!okclus(v, v->root)) return FSR_EBADFS;
    x_root(v, &r);
    for (;;) {
        int n = x_sread(v, &r, (uint64_t)i++ * 32, e, 32);
        if (n < 0) return n;
        if (n < 32 || !e[0]) break;
        if (e[0] == 0x81 && !(e[1] & 1) && !v->bmp_clus) { v->bmp_clus = LE32(e + 20); v->bmp_len = LE64(e + 24); }
        if (e[0] == 0x82) { upclus = LE32(e + 20); uplen = LE64(e + 24); }
    }
    if (upclus) {   /* compressed up-case table: FFFFh n = n identity entries */
        uint32_t ch = 0; uint64_t p = 0;
        memset(&up, 0, sizeof up);
        up.first = upclus; up.size = up.vdl = uplen;
        while (ch < 256 && p + 2 <= uplen) {
            uint8_t w[4];
            if (x_sread(v, &up, p, w, 2) != 2) return FSR_EIO;
            p += 2;
            if (LE16(w) == 0xFFFF) {
                if (x_sread(v, &up, p, w, 2) != 2) return FSR_EIO;
                p += 2; ch += LE16(w);
            } else v->up[ch++] = LE16(w);
        }
    }
    return 0;
}

int fsr_x_open(fsr_vol *v, const char *path, fsr_file *f, fsr_dirent *info)
{
    fsr_xstream cur; uint16_t c[256]; char n83[13]; int cl, r;
    xent e; int isdir = 1;
    x_root(v, &cur);
    f->attr = FSR_ATTR_DIR; info->attr = FSR_ATTR_DIR;
    while ((r = fsr_comp(&path, c, &cl, n83)) == 1) {
        uint32_t i = 0, tn = fsr_tilde(n83); uint16_t h = x_hash(v, c, cl);
        int found = 0; fsr_tscan ts;
        if (!isdir) return FSR_ENOPATH;
        ts.k = 0;
        for (;;) {
            if ((r = x_next(v, &cur, &i, &e)) < 0) return r;
            if (!r) break;
            if (e.hash == h && !fsr_namecmp(v, c, cl, v->nm, v->nmlen)) found = 1;
            else if (tn && !fsr_valid83(v, 0)) {
                int t = fsr_tilde_hit(v, &ts, n83, tn);
                if (t < 0) {
                    char key[10], al[13]; uint32_t n, st = e.start;
                    fsr_aliaskey(v, key); fsr_alias(key, tn, al);
                    if ((r = x_aliasnum(v, &cur, st, key, &n)) < 0) return r;
                    t = n == tn;
                    if (t && (r = x_next(v, &cur, &st, &e)) <= 0) return r ? r : FSR_EBADFS;
                }
                if (t) found = 2;
            }
            if (found) break;
        }
        if (!found) { while (*path == '\\' || *path == '/') path++; return *path ? FSR_ENOPATH : FSR_ENOFILE; }
        fsr_mkname(v, info);
        if ((r = x_name83(v, &cur, e.start, info->name)) < 0) return r;
        x_fill(&e, info);
        isdir = (e.attr & FSR_ATTR_DIR) != 0;
        cur = e.s; cur.clus = 0;
        if (isdir) cur.vdl = cur.size;
    }
    if (r < 0) return r;
    f->attr = info->attr; f->date = info->date; f->time = info->time; f->size = info->size;
    f->u.x = cur;
    return 0;
}

int32_t fsr_x_read(fsr_vol *v, fsr_file *f, void *buf, uint32_t len)
{
    return x_sread(v, &f->u.x, f->pos, buf, len);
}

int fsr_x_find(fsr_vol *v, const char *dir, fsr_find *s, fsr_dirent *d, int first)
{
    int r; xent e;
    if (first) {
        fsr_file f;
        if ((r = fsr_open(v, dir, &f, 0)) < 0) return r == FSR_ENOFILE ? FSR_ENOPATH : r;
        if (!(f.attr & FSR_ATTR_DIR)) return FSR_ENOPATH;
        s->u.x.d = f.u.x; s->u.x.idx = 0;
    }
    for (;;) {
        if ((r = x_next(v, &s->u.x.d, &s->u.x.idx, &e)) < 0) return r;
        if (!r) return FSR_ENOMORE;
        s->ord++;
        if (!fsr_wantattr(s->amask, e.attr)) continue;
        memset(d, 0, sizeof *d);
        fsr_mkname(v, d);
        if ((r = x_name83(v, &s->u.x.d, e.start, d->name)) < 0) return r;
        if (!fsr_match(s->pat, d->name)) continue;
        x_fill(&e, d);
        return 0;
    }
}

int fsr_x_free(fsr_vol *v, uint64_t *nfree)
{
    fsr_xstream s; uint8_t b[256]; uint64_t p = 0, c = 0; int32_t n;
    memset(&s, 0, sizeof s);
    s.first = v->bmp_clus; s.size = s.vdl = v->bmp_len;
    while ((n = x_sread(v, &s, p, b, sizeof b)) > 0) { c += fsr_popfree(b, (uint32_t)n, p * 8, v->nclus); p += (uint32_t)n; }
    if (n < 0) return n;
    *nfree = c;
    return 0;
}
