/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* fsread: NTFS reader. See fsread.h. Read-only, C99, no libc beyond memcpy/memcmp/memset.
 * MFT records and INDX blocks are read on demand into the caller's work buffer
 * (reca = base record, recb = extension record via $ATTRIBUTE_LIST, idx = one index
 * node or a copy of $INDEX_ROOT); only the $MFT runlist is kept (FSR_MFT_RUNS runs). */
#include <string.h>
#include "fsread.h"

#define LE16 fsr_le16
#define LE32 fsr_le32
#define LE64 fsr_le64
#define NOTAG (~(uint64_t)0)
#define REFMASK 0xFFFFFFFFFFFFull
#define ROOTVCN NOTAG

#define AT_SI    0x10
#define AT_LIST  0x20
#define AT_FN    0x30
#define AT_VOL   0x70
#define AT_DATA  0x80
#define AT_IROOT 0x90
#define AT_IALLOC 0xA0

static const uint16_t I30[4] = { '$', 'I', '3', '0' };

static int fixup(uint8_t *b, uint32_t size, const char *sig)
{
    uint32_t uo = LE16(b + 4), uc = LE16(b + 6), i;
    if (memcmp(b, sig, 4) || uc < 2 || uo + 2 * uc > size || (uc - 1) * 512 > size) return FSR_EBADFS;
    for (i = 1; i < uc; i++) {
        uint8_t *p = b + i * 512 - 2;
        if (p[0] != b[uo] || p[1] != b[uo + 1]) return FSR_EBADFS;
        p[0] = b[uo + 2 * i]; p[1] = b[uo + 2 * i + 1];
    }
    return 0;
}

/* runlist of a non-resident attribute: the run containing vcn */
static int rl_map(const uint8_t *a, uint64_t vcn, fsr_run *out)
{
    const uint8_t *p = a + LE16(a + 32), *end = a + LE32(a + 4);
    uint64_t cv = LE64(a + 16); int64_t lcn = 0;
    while (p < end && *p) {
        int ln = *p & 15, lo = *p >> 4, i; uint64_t len = 0; int64_t d = 0;
        p++;
        if (!ln || ln > 8 || lo > 8 || p + ln + lo > end) return FSR_EBADFS;
        for (i = 0; i < ln; i++) len |= (uint64_t)p[i] << (8 * i);
        p += ln;
        if (lo) {
            for (i = 0; i < lo; i++) d |= (int64_t)((uint64_t)p[i] << (8 * i));
            if (lo < 8 && (p[lo - 1] & 0x80)) d |= (int64_t)(~(uint64_t)0 << (8 * lo));
            lcn += d; p += lo;
        }
        if (vcn >= cv && vcn - cv < len) { out->vcn = cv; out->len = len; out->lcn = lo ? lcn : -1; return 0; }
        cv += len;
    }
    return FSR_EBADFS;
}

static int mft_map(fsr_vol *v, uint64_t vcn, fsr_run *r)
{
    int i;
    for (i = 0; i < v->nmft; i++)
        if (vcn >= v->mft[i].vcn && vcn - v->mft[i].vcn < v->mft[i].len) { *r = v->mft[i]; return 0; }
    return FSR_EBADFS;
}

static int n_rec(fsr_vol *v, uint8_t *buf, uint64_t *tag, uint64_t mft)
{
    uint64_t off = mft * v->rec_size; uint32_t done = 0; int r;
    if (*tag == mft) return 0;
    *tag = NOTAG;
    while (done < v->rec_size) {
        fsr_run run; uint64_t lba, avail; uint32_t n, co = (uint32_t)(off & (v->cs - 1));
        if ((r = mft_map(v, off >> v->cs_shift, &run))) return r;
        if (run.lcn < 0) return FSR_EBADFS;
        lba = v->base + ((uint64_t)(run.lcn + ((off >> v->cs_shift) - run.vcn)) << (v->cs_shift - v->ss_shift)) + (co >> v->ss_shift);
        avail = ((run.vcn + run.len - (off >> v->cs_shift)) << v->cs_shift) - co;
        n = v->rec_size - done;
        if (n > avail) n = (uint32_t)avail;
        if (v->rec_size >= v->ss) {
            if (v->rd(v->ctx, lba, (int)(n >> v->ss_shift), buf + done)) return FSR_EIO;
        } else {
            if ((r = fsr_sread(v, lba))) return r;
            memcpy(buf + done, v->sec + (off & (v->ss - 1)), n);
        }
        done += n; off += n;
    }
    if ((r = fixup(buf, v->rec_size, "FILE"))) return r;
    *tag = mft;
    return 0;
}

static uint8_t *a_next(fsr_vol *v, uint8_t *rec, uint8_t *prev)
{
    uint32_t off = prev ? (uint32_t)(prev - rec) + LE32(prev + 4) : LE16(rec + 20), len;
    if (off + 16 > v->rec_size || LE32(rec + off) == 0xFFFFFFFFu) return 0;
    len = LE32(rec + off + 4);
    if (len < 16 || off + len > v->rec_size) return 0;
    return rec + off;
}

static int a_is(const uint8_t *a, uint32_t type, const uint16_t *nm, int nl, uint64_t vcn)
{
    if (LE32(a) != type || a[9] != nl || (nl && memcmp(a + LE16(a + 10), nm, 2 * (size_t)nl))) return 0;
    return !a[8] || (vcn >= LE64(a + 16) && vcn <= LE64(a + 24));
}

static int32_t n_xread(fsr_vol *v, const uint8_t *hdr, uint64_t mft, uint32_t type, const uint16_t *nm, int nl,
                       uint64_t pos, void *buf, uint32_t len, fsr_run *rc);

/* attribute (type, name) of record mft covering vcn, via $ATTRIBUTE_LIST if needed */
static int a_find(fsr_vol *v, uint64_t mft, uint32_t type, const uint16_t *nm, int nl, uint64_t vcn, uint8_t **out)
{
    uint8_t *a = 0, *al = 0; uint64_t ref = NOTAG, pos = 0, alen; int r;
    if ((r = n_rec(v, v->reca, &v->reca_tag, mft))) return r;
    while ((a = a_next(v, v->reca, a))) {
        if (LE32(a) == AT_LIST) al = a;
        if (a_is(a, type, nm, nl, vcn)) { *out = a; return 0; }
    }
    if (!al) return FSR_ENOFILE;
    alen = al[8] ? LE64(al + 48) : LE32(al + 16);
    while (pos + 26 <= alen) {
        uint8_t e[64]; uint32_t n = alen - pos < 64 ? (uint32_t)(alen - pos) : 64, rl, t;
        if (al[8]) { int32_t g = n_xread(v, al, 0, 0, 0, 0, pos, e, n, 0); if (g < 0) return g; }
        else memcpy(e, al + LE16(al + 20) + pos, n);
        rl = LE16(e + 4); t = LE32(e);
        if (rl < 26) break;
        if (t > type) break;
        if (t == type && e[6] == nl && (!nl || (e[7] + 2u * nl <= n && !memcmp(e + e[7], nm, 2 * (size_t)nl)))
            && LE64(e + 8) <= vcn)
            ref = LE64(e + 16) & REFMASK;
        pos += rl;
    }
    if (ref == NOTAG || ref == mft) return FSR_ENOFILE;
    if ((r = n_rec(v, v->recb, &v->recb_tag, ref))) return r;
    a = 0;
    while ((a = a_next(v, v->recb, a))) if (a_is(a, type, nm, nl, vcn)) { *out = a; return 0; }
    return FSR_ENOFILE;
}

/* read attribute bytes. hdr != NULL: that (single-extent) attribute header, else
 * look up (mft, type, name). rc: optional run cache. Sparse runs read as 0. */
static int32_t n_xread(fsr_vol *v, const uint8_t *hdr, uint64_t mft, uint32_t type, const uint16_t *nm, int nl,
                       uint64_t pos, void *buf, uint32_t len, fsr_run *rc)
{
    uint8_t *d = (uint8_t *)buf, *a; uint32_t done = 0; int r;
    while (done < len) {
        uint64_t vcn = pos >> v->cs_shift, avail, lba; uint32_t n = len - done, co = (uint32_t)(pos & (v->cs - 1)), so;
        fsr_run run;
        if (rc && rc->len && vcn >= rc->vcn && vcn - rc->vcn < rc->len) run = *rc;
        else {
            if (hdr) a = (uint8_t *)hdr;
            else if ((r = a_find(v, mft, type, nm, nl, vcn, &a))) return r;
            if (!a[8]) {   /* resident */
                uint32_t vl = LE32(a + 16);
                if (pos >= vl) break;
                if (n > vl - pos) n = (uint32_t)(vl - pos);
                memcpy(d, a + LE16(a + 20) + pos, n);
                done += n;
                break;
            }
            if (LE16(a + 12) & 0x40FF) return FSR_EACCES;   /* compressed / encrypted */
            if ((r = rl_map(a, vcn, &run))) return r;
            if (rc) *rc = run;
        }
        avail = ((run.vcn + run.len - vcn) << v->cs_shift) - co;
        if (n > avail) n = (uint32_t)avail;
        if (run.lcn < 0) memset(d, 0, n);
        else {
            lba = v->base + ((uint64_t)(run.lcn + (vcn - run.vcn)) << (v->cs_shift - v->ss_shift)) + (co >> v->ss_shift);
            so = (uint32_t)(pos & (v->ss - 1));
            if (!so && n >= v->ss) {
                uint32_t cnt = n >> v->ss_shift;
                if (cnt > 0x4000) cnt = 0x4000;
                if (v->rd(v->ctx, lba, (int)cnt, d)) return FSR_EIO;
                n = cnt << v->ss_shift;
            } else {
                if ((r = fsr_sread(v, lba))) return r;
                if (n > v->ss - so) n = v->ss - so;
                memcpy(d, v->sec + so, n);
            }
        }
        d += n; pos += n; done += n;
    }
    return (int32_t)done;
}

static void dostime(uint64_t ft, uint16_t *date, uint16_t *time)
{
    int64_t t = (int64_t)(ft / 10000000u) - 11644473600LL, days, y, m, doy, yoe, era, dd, mp;
    uint32_t s;
    if (t < 315532800LL) t = 315532800LL;   /* 1980-01-01 */
    days = t / 86400; s = (uint32_t)(t % 86400);
    days += 719468; era = days / 146097; dd = days - era * 146097;   /* civil_from_days */
    yoe = (dd - dd / 1460 + dd / 36524 - dd / 146096) / 365;
    y = yoe + era * 400; doy = dd - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153; dd = doy - (153 * mp + 2) / 5 + 1; m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) y++;
    if (y > 2107) y = 2107;
    *date = (uint16_t)((y - 1980) << 9 | m << 5 | dd);
    *time = (uint16_t)((s / 3600) << 11 | (s / 60 % 60) << 5 | (s % 60) / 2);
}

/* record info -> d (attr, size, date/time, DOS-namespace name if any for parent dir).
 * Returns 1 if a DOS name was found, 0 if not, < 0 error. */
static int n_info(fsr_vol *v, uint64_t mft, uint64_t dir, fsr_dirent *d, uint8_t **data)
{
    uint8_t *a = 0; int r, dos = 0; uint16_t fl;
    if ((r = n_rec(v, v->reca, &v->reca_tag, mft))) return r;
    fl = LE16(v->reca + 22);
    if (!(fl & 1)) return FSR_ENOFILE;
    d->attr = 0; d->size = 0;
    while ((a = a_next(v, v->reca, a))) {
        const uint8_t *val = a + LE16(a + 20);
        if (a[8]) continue;
        if (LE32(a) == AT_SI) { d->attr = (uint8_t)(LE32(val + 32) & 0x27); dostime(LE64(val + 8), &d->date, &d->time); }
        if (LE32(a) == AT_FN && val[65] == 2 && (LE64(val) & REFMASK) == dir && val[64] <= 12) {
            int i;
            for (i = 0; i < val[64]; i++) d->name[i] = (char)LE16(val + 66 + 2 * i);
            d->name[i] = 0; dos = 1;
        }
        if (LE32(a) == AT_FN && val[65] != 2 && (LE64(val) & REFMASK) == dir && !d->lname[0]) {
            int i;
            v->nmlen = val[64];
            for (i = 0; i < v->nmlen; i++) v->nm[i] = LE16(val + 66 + 2 * i);
            fsr_mkname(v, d);
        }
    }
    if (data) *data = 0;
    if (fl & 2) d->attr |= FSR_ATTR_DIR;
    else {
        if ((r = a_find(v, mft, AT_DATA, 0, 0, 0, &a))) return r == FSR_ENOFILE ? 0 : r;   /* no $DATA: size 0 */
        d->size = a[8] ? LE64(a + 48) : LE32(a + 16);
        if (data) *data = a;
    }
    return dos;
}

/* load index node (ROOTVCN = $INDEX_ROOT copy) of dir into v->idx; *ho = header offset */
static int n_node(fsr_vol *v, uint64_t dir, uint64_t vcn, uint32_t *ho, uint32_t *end)
{
    int r; uint32_t bufsz = v->idx_size > v->rec_size ? v->idx_size : v->rec_size;
    if (v->idx_dir != dir || v->idx_tag != vcn) {
        v->idx_tag = NOTAG;
        if (vcn == ROOTVCN) {
            uint8_t *a; uint32_t vl;
            if ((r = a_find(v, dir, AT_IROOT, I30, 4, 0, &a))) return r == FSR_ENOFILE ? FSR_ENOPATH : r;
            vl = LE32(a + 16);
            if (a[8] || vl < 32 || vl - 16 > bufsz) return FSR_EBADFS;
            memcpy(v->idx, a + LE16(a + 20) + 16, vl - 16);
        } else {
            int32_t g = n_xread(v, 0, dir, AT_IALLOC, I30, 4, vcn * v->vcn_size, v->idx, v->idx_size, 0);
            if (g < 0) return g;
            if ((uint32_t)g != v->idx_size) return FSR_EBADFS;
            if ((r = fixup(v->idx, v->idx_size, "INDX"))) return r;
        }
        v->idx_dir = dir; v->idx_tag = vcn;
    }
    *ho = vcn == ROOTVCN ? 0 : 24;
    *end = *ho + LE32(v->idx + *ho + 4);
    if (*end > bufsz || LE32(v->idx + *ho) < 16) return FSR_EBADFS;
    return 0;
}

/* in-order B+ tree walk: next index entry (pointer into v->idx), 1 / 0 end / < 0 */
static int n_step(fsr_vol *v, fsr_find *s, uint8_t **pe)
{
    for (;;) {
        uint32_t ho, end, len; uint16_t fl; uint8_t *e; int r, sp = s->u.n.sp;
        if (!sp) return 0;
        if ((r = n_node(v, s->u.n.dir, s->u.n.st[sp - 1].vcn, &ho, &end))) return r;
        if (!s->u.n.st[sp - 1].off) s->u.n.st[sp - 1].off = ho + LE32(v->idx + ho);
        if (s->u.n.st[sp - 1].off + 16u > end) return FSR_EBADFS;
        e = v->idx + s->u.n.st[sp - 1].off;
        len = LE16(e + 8); fl = LE16(e + 12);
        if (len < 16 || s->u.n.st[sp - 1].off + len > end) return FSR_EBADFS;
        if ((fl & 1) && !s->u.n.st[sp - 1].down) {
            s->u.n.st[sp - 1].down = 1;
            if (sp >= FSR_DEPTH) return FSR_EBADFS;
            s->u.n.st[sp].vcn = LE64(e + len - 8); s->u.n.st[sp].off = 0; s->u.n.st[sp].down = 0;
            s->u.n.sp++;
            continue;
        }
        if (fl & 2) { s->u.n.sp--; continue; }
        if (len < 16 + 66 || LE16(e + 10) < 66 || 66 + 2u * e[16 + 64] > LE16(e + 10)) return FSR_EBADFS;
        s->u.n.st[sp - 1].off += len;
        s->u.n.st[sp - 1].down = 0;
        *pe = e;
        return 1;
    }
}

static void n_start(fsr_find *s, uint64_t dir)
{
    s->u.n.dir = dir; s->u.n.sp = 1;
    s->u.n.st[0].vcn = ROOTVCN; s->u.n.st[0].off = 0; s->u.n.st[0].down = 0;
}

/* entry -> v->nm; returns its MFT number, or NOTAG for entries DOS never sees */
static uint64_t n_ent(fsr_vol *v, const uint8_t *e, int *ns)
{
    const uint8_t *fn = e + 16; uint64_t ref = LE64(e) & REFMASK; int i;
    *ns = fn[65];
    if (*ns == 2 || ref < 16) return NOTAG;   /* DOS-only twin / metadata files, "." */
    v->nmlen = fn[64];
    for (i = 0; i < v->nmlen; i++) v->nm[i] = LE16(fn + 66 + 2 * i);
    return ref;
}

/* alias number of the ord-th visible entry of dir with basis key */
static int n_aliasnum(fsr_vol *v, uint64_t dir, uint32_t ord, const char *key, uint32_t *n)
{
    fsr_find s; uint8_t *e; uint32_t p = 0, cnt = 1; char k2[10]; int r, ns;
    fsr_acache *c = fsr_acget(v, dir, key, ord);
    if (c && c->pos == ord) { *n = c->n; return 0; }
    if (c) { s = c->st; p = c->pos + 1; cnt = c->n + 1; } else n_start(&s, dir);
    while ((r = n_step(v, &s, &e)) == 1) {
        if (n_ent(v, e, &ns) == NOTAG) continue;
        if (p == ord) { fsr_acput(v, dir, key, ord, cnt, &s); break; }
        p++;
        if (ns == 3 || fsr_valid83(v, 0)) continue;
        fsr_aliaskey(v, k2);
        if (!memcmp(k2, key, 9)) cnt++;
    }
    *n = cnt;
    return r < 0 ? r : 0;
}

/* full dirent for entry ref (v->nm = its name, ord = its position) */
static int n_dirent(fsr_vol *v, uint64_t dir, uint64_t ref, uint32_t ord, fsr_dirent *d)
{
    int r; char key[10]; uint32_t n;
    memset(d, 0, sizeof *d);
    fsr_mkname(v, d);
    if (fsr_valid83(v, d->name)) { if ((r = n_info(v, ref, dir, d, 0)) < 0) return r; return 0; }
    fsr_aliaskey(v, key);
    if ((r = n_info(v, ref, dir, d, 0)) < 0) return r;
    if (r == 1) return 0;
    if ((r = n_aliasnum(v, dir, ord, key, &n)) < 0) return r;
    fsr_alias(key, n, d->name);
    return 0;
}

/* B+ tree descent by name (long or DOS name). 1 found, 0 not, < 0 error */
static int n_lookup(fsr_vol *v, uint64_t dir, const uint16_t *c, int cl, uint64_t *ref, int *ns)
{
    uint64_t vcn = ROOTVCN; int depth, r;
    for (depth = 0; depth < FSR_DEPTH; depth++) {
        uint32_t ho, end, off; int next = 0;
        if ((r = n_node(v, dir, vcn, &ho, &end))) return r;
        off = ho + LE32(v->idx + ho);
        while (!next) {
            uint8_t *e = v->idx + off; uint32_t len; uint16_t fl; int k;
            if (off + 16 > end) return FSR_EBADFS;
            len = LE16(e + 8); fl = LE16(e + 12);
            if (len < 16 || off + len > end) return FSR_EBADFS;
            if (!(fl & 2)) {
                uint16_t nm[255]; int i, nl = e[16 + 64];
                if (len < 16 + 66 + 2u * nl) return FSR_EBADFS;
                for (i = 0; i < nl; i++) nm[i] = LE16(e + 16 + 66 + 2 * i);
                k = fsr_namecmp(v, c, cl, nm, nl);
                if (!k) {   /* matched name -> v->nm */
                    *ref = LE64(e) & REFMASK; *ns = e[16 + 65];
                    memcpy(v->nm, nm, 2 * (size_t)nl); v->nmlen = nl;
                    return 1;
                }
            } else k = -1;
            if (k < 0) {
                if (!(fl & 1)) return 0;
                vcn = LE64(e + len - 8); next = 1;
            } else off += len;
        }
    }
    return FSR_EBADFS;
}

static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

int fsr_n_mount(fsr_vol *v, uint8_t *b, uint32_t ws)
{
    uint32_t spc = b[13], need; int8_t cr = (int8_t)b[64], ci = (int8_t)b[68]; uint64_t vcn = 0, alloc = 0;
    uint8_t *a; int r, i; uint8_t w[512];
    v->ss = LE16(b + 11);
    if (v->ss < 512 || v->ss > 4096 || (v->ss & (v->ss - 1))) return FSR_EBADFS;
    for (v->ss_shift = 9; (1u << v->ss_shift) < v->ss; v->ss_shift++) ;
    if (spc > 0x80) { if (256 - spc > 31) return FSR_EBADFS; spc = 1u << (256 - spc); }
    if (!spc || (spc & (spc - 1)) || (uint64_t)spc * v->ss > (1u << 21)) return FSR_EBADFS;
    v->cs = spc * v->ss;
    for (v->cs_shift = v->ss_shift; (1u << v->cs_shift) < v->cs; v->cs_shift++) ;
    v->nclus = LE64(b + 40) / spc;
    v->rec_size = cr > 0 ? (uint32_t)cr * v->cs : (cr > -31 && cr < 0 ? 1u << -cr : 0);
    v->idx_size = ci > 0 ? (uint32_t)ci * v->cs : (ci > -31 && ci < 0 ? 1u << -ci : 0);
    if (v->rec_size < 512 || v->rec_size > 4096 || v->idx_size < 512 || v->idx_size > 65536) return FSR_EBADFS;
    v->vcn_size = v->idx_size >= v->cs ? v->cs : 512;
    need = v->ss + 2 * v->rec_size + (v->idx_size > v->rec_size ? v->idx_size : v->rec_size);
    if (ws < need) return FSR_ENOMEM;
    v->sec = b; v->reca = b + v->ss; v->recb = v->reca + v->rec_size; v->idx = v->recb + v->rec_size;
    v->sec_tag = v->reca_tag = v->recb_tag = v->idx_tag = NOTAG;
    /* $MFT runlist: start with "whole volume from MFT LCN", replace with the real runs */
    v->nmft = 1; v->mft[0].vcn = 0; v->mft[0].len = (uint64_t)1 << 40; v->mft[0].lcn = (int64_t)LE64(b + 48);
    for (;;) {
        const uint8_t *p, *end; uint64_t cv; int64_t lcn = 0; fsr_run tmp[FSR_MFT_RUNS]; int nt = 0;
        if ((r = a_find(v, 0, AT_DATA, 0, 0, vcn, &a))) { if (vcn) break; return r == FSR_ENOFILE ? FSR_EBADFS : r; }
        if (!a[8]) return FSR_EBADFS;
        if (!LE64(a + 16)) alloc = LE64(a + 40);
        p = a + LE16(a + 32); end = a + LE32(a + 4); cv = LE64(a + 16);
        while (p < end && *p) {   /* decode the whole extent */
            int ln = *p & 15, lo = *p >> 4, k; uint64_t len = 0; int64_t d = 0;
            p++;
            if (!ln || ln > 8 || lo > 8 || p + ln + lo > end) return FSR_EBADFS;
            for (k = 0; k < ln; k++) len |= (uint64_t)p[k] << (8 * k);
            p += ln;
            for (k = 0; k < lo; k++) d |= (int64_t)((uint64_t)p[k] << (8 * k));
            if (lo && lo < 8 && (p[lo - 1] & 0x80)) d |= (int64_t)(~(uint64_t)0 << (8 * lo));
            p += lo; lcn += d;
            if (!lo) return FSR_EBADFS;
            if (nt >= FSR_MFT_RUNS) return FSR_ENOMEM;
            tmp[nt].vcn = cv; tmp[nt].len = len; tmp[nt].lcn = lcn; nt++;
            cv += len;
        }
        vcn = LE64(a + 24) + 1;
        if (!LE64(a + 16)) { memcpy(v->mft, tmp, sizeof(fsr_run) * nt); v->nmft = nt; }
        else {
            if (v->nmft + nt > FSR_MFT_RUNS) return FSR_ENOMEM;
            memcpy(v->mft + v->nmft, tmp, sizeof(fsr_run) * nt); v->nmft += nt;
        }
        if (vcn << v->cs_shift >= alloc) break;
    }
    v->reca_tag = v->recb_tag = NOTAG;
    /* dirty flag ($Volume $VOLUME_INFORMATION) */
    if (!a_find(v, 3, AT_VOL, 0, 0, 0, &a) && !a[8] && LE32(a + 16) >= 12)
        v->dirty = (LE16(a + LE16(a + 20) + 10) & 1) != 0;
    /* $UpCase, first 256 entries */
    if (n_xread(v, 0, 10, AT_DATA, 0, 0, 0, w, 512, 0) == 512)
        for (i = 0; i < 256; i++) v->up[i] = LE16(w + 2 * i);
    return 0;
}

/* resolve path -> MFT number (+ dirent) */
static int n_path(fsr_vol *v, const char *path, uint64_t *out, fsr_dirent *info)
{
    uint64_t cur = 5, ref = 0; uint16_t c[256]; char n83[13]; int cl, r, isdir = 1;
    info->attr = FSR_ATTR_DIR;
    while ((r = fsr_comp(&path, c, &cl, n83)) == 1) {
        int found, ns = 0;
        if (!isdir) return FSR_ENOPATH;
        if ((found = n_lookup(v, cur, c, cl, &ref, &ns)) < 0) return found;
        if (found) {
            memset(info, 0, sizeof *info);
            if (ns != 2) fsr_mkname(v, info);
            if ((r = n_info(v, ref, cur, info, 0)) < 0) return r;
            if (!info->lname[0]) fsr_mkname(v, info);
            if (!r && !fsr_valid83(v, info->name)) {   /* generated alias: needs its position */
                fsr_find s; uint8_t *e; uint32_t ord = 0, n; int ns; uint64_t m; char key[10];
                fsr_aliaskey(v, key);
                n_start(&s, cur);
                while ((r = n_step(v, &s, &e)) == 1) {
                    if ((m = n_ent(v, e, &ns)) == NOTAG) continue;
                    if (m == ref) break;
                    ord++;
                }
                if (r < 0) return r;
                if ((r = n_aliasnum(v, cur, ord, key, &n)) < 0) return r;
                fsr_alias(key, n, info->name);
            }
        } else if (fsr_tilde(n83)) {   /* generated alias? */
            fsr_find s; uint8_t *e; uint32_t ord = 0; int ns, t; fsr_tscan ts;
            ts.k = 0;
            n_start(&s, cur);
            while ((r = n_step(v, &s, &e)) == 1) {
                if ((ref = n_ent(v, e, &ns)) == NOTAG) continue;
                ord++;
                if (ns == 3 || fsr_valid83(v, 0)) continue;
                if (!(t = fsr_tilde_hit(v, &ts, n83, fsr_tilde(n83)))) continue;
                if ((r = n_dirent(v, cur, ref, ord - 1, info)) < 0) return r;
                if (streq(info->name, n83)) { found = 1; break; }
            }
            if (r < 0) return r;
        }
        if (!found) { while (*path == '\\' || *path == '/') path++; return *path ? FSR_ENOPATH : FSR_ENOFILE; }
        isdir = (info->attr & FSR_ATTR_DIR) != 0;
        cur = ref;
    }
    if (r < 0) return r;
    *out = cur;
    return 0;
}

int fsr_n_open(fsr_vol *v, const char *path, fsr_file *f, fsr_dirent *info)
{
    uint64_t m; int r; uint8_t *a;
    if ((r = n_path(v, path, &m, info))) return r;
    if ((r = n_info(v, m, NOTAG, info, &a)) < 0) return r;
    if (m == 5) { info->name[0] = 0; info->lname[0] = 0; }
    f->attr = info->attr; f->date = info->date; f->time = info->time; f->size = info->size;
    f->u.n.mft = m; f->u.n.init = f->size;
    if (!(info->attr & FSR_ATTR_DIR) && a) {
        if (a[8]) {
            if (LE16(a + 12) & 0x40FF) return FSR_EACCES;
            f->u.n.init = LE64(a + 56);
            if (f->u.n.init > f->size) f->u.n.init = f->size;
        } else f->u.n.resident = 1;
    }
    return 0;
}

int32_t fsr_n_read(fsr_vol *v, fsr_file *f, void *buf, uint32_t len)
{
    uint32_t n = len; int32_t g = 0;
    if (f->pos < f->u.n.init) {
        if (n > f->u.n.init - f->pos) n = (uint32_t)(f->u.n.init - f->pos);
        g = n_xread(v, 0, f->u.n.mft, AT_DATA, 0, 0, f->pos, buf, n, &f->u.n.rc);
        if (g < 0) return g;
        if ((uint32_t)g < n) return g;
    }
    memset((uint8_t *)buf + g, 0, len - (uint32_t)g);
    return (int32_t)len;
}

int fsr_n_find(fsr_vol *v, const char *dir, fsr_find *s, fsr_dirent *d, int first)
{
    int r, ns; uint8_t *e; uint64_t ref;
    if (first) {
        uint64_t m; fsr_dirent t;
        if ((r = n_path(v, dir ? dir : "", &m, &t))) return r == FSR_ENOFILE ? FSR_ENOPATH : r;
        if (!(t.attr & FSR_ATTR_DIR)) return FSR_ENOPATH;
        n_start(s, m);
    }
    for (;;) {
        if ((r = n_step(v, s, &e)) < 0) return r;
        if (!r) return FSR_ENOMORE;
        if ((ref = n_ent(v, e, &ns)) == NOTAG) continue;
        s->ord++;
        if ((r = n_dirent(v, s->u.n.dir, ref, s->ord - 1, d)) < 0) { if (r == FSR_ENOFILE) continue; return r; }
        if (!fsr_wantattr(s->amask, d->attr) || !fsr_match(s->pat, d->name)) continue;
        return 0;
    }
}

int fsr_n_free(fsr_vol *v, uint64_t *nfree)
{
    uint8_t b[512]; uint64_t p = 0, c = 0, nb = (v->nclus + 7) / 8; fsr_run rc; int32_t g;
    rc.len = 0;
    while (p < nb) {
        uint32_t n = nb - p < sizeof b ? (uint32_t)(nb - p) : (uint32_t)sizeof b;
        if ((g = n_xread(v, 0, 6, AT_DATA, 0, 0, p, b, n, &rc)) <= 0) return g ? g : FSR_EBADFS;
        c += fsr_popfree(b, (uint32_t)g, p * 8, v->nclus); p += (uint32_t)g;
    }
    *nfree = c;
    return 0;
}
