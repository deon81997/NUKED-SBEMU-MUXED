/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* USBREDIR - exFAT / NTFS USB partitions as read-only DOS drives, see usbredir.h and docs/USB-STORAGE.md 5b / 6.
 *
 * DOS has no exFAT / NTFS code, so these drives are "network" drives: CDS flags C000h (network + physical), path
 * "X:\", and DOS sends every file call for them to INT 2Fh AH=11h. usbredir.asm takes the calls for our letters
 * (and chains all others in real mode) and far-calls UR_Req through a DPMI real-mode callback. File data comes from
 * sbemu/fsread (8.3 names: NTFS DOS names or generated aliases) over USBPAD_DiskRead with a 4 KB-line cache.
 * Read-only: create / delete / rename / mkdir / rmdir / set attributes / write -> access denied (05h); files may be
 * opened for writing (programs often open data files read/write) but every write fails.
 * State: open files in a slot table (SFT 0Bh = slot, 1Bh = id), searches in a slot table (search data block
 * 0Dh = slot, 0Fh = 'US', 11h = id; DOS never ends a search, so the oldest slot is reused).
 * Partitions: superfloppy, MBR primaries + logicals, GPT; any whose boot sector says EXFAT / NTFS (any sector size).
 * A stick unplugged -> "not ready"; plugged back (new gen, same disk number) or a card swapped (boot sector CRC
 * after 2 s idle) -> mounted again, files open before that answer "invalid handle". GPL-2.0-or-later. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "dpmi/dpmi.h"
#include "usbpad.h"
#include "usbredir.h"
#include "usbdisk.h"
#include "usbredirbin.h"
#include "fsread/fsread.h"

#define UR_UNITS 8
#define UR_FILES 48
#define UR_FINDS 24
#define UR_STUB  0x100
#define UR_CDS   0x58
#define MAGIC    0x5553

typedef struct
{
    int disk, lun, idx;        /* usbpad disk number (followed by gen + lun), nth exFAT/NTFS partition on it */
    uint32_t gen, start;
    int drive, ok;
    uint32_t mgen;             /* mount generation: files / searches from an older one are stale */
    uint32_t sig, last;
    int freeok; uint64_t nfree;
    fsr_vol v;
    uint8_t work[FSR_WORK_MIN];
} RUnit;

typedef struct { int used, unit; uint32_t mgen, id; fsr_file f; } RFile;
typedef struct { int used, unit, phase, pend; uint32_t mgen, id, age; fsr_find s; fsr_dirent pd; uint16_t date, time; } RFind;

static RUnit* RU[UR_UNITS];
static int NR;
static RFile FL[UR_FILES];
static RFind FD[UR_FINDS];
static uint32_t Ids, Age;
static uint16_t Seg;
static uint32_t Cb, Sda, CdsBase;
static DPMI_REG Reg;
static char Letters[3 * UR_UNITS + 1], Msg[120];
static uint8_t Bounce[32768];
static uint8_t Blk[4096];

/* ---- sector cache: 32 lines of 4 KB (aligned, device blocks) ---- */
#define CL_N 32
static struct { int disk, valid; uint32_t lba, used; } CL[CL_N];
static uint8_t CLData[CL_N][4096];
static uint32_t CLClock;

static void cache_drop(int disk) { int i; for (i = 0; i < CL_N; ++i) if (CL[i].disk == disk) CL[i].valid = 0; }

static int dread(int disk, uint32_t lba, int n, void* buf)
{
    int r, t;
    for (t = 0; t < 200; ++t)
    {
        r = USBPAD_DiskRead(disk, lba, n, buf);
        if (r != USBPAD_E_BUSY) return r;
        { uint32_t t0 = DPMI_LoadD(0x46C); while (DPMI_LoadD(0x46C) == t0) {} }
    }
    return r;
}

/* fsread's read callback: absolute device blocks */
static int rcb(void* ctx, uint64_t lba, int count, void* buf)
{
    RUnit* u = (RUnit*)ctx;
    const USBPAD_Disk* d = USBPAD_DiskInfo(u->disk);
    uint32_t spl, base;
    int i, v = -1;
    if (!d || !d->ready || d->bsize > 4096 || lba + count > d->blocks) return -1;
    spl = 4096 / d->bsize;
    base = (uint32_t)lba - (uint32_t)lba % spl;
    if ((uint32_t)lba + count > base + spl || base + spl > d->blocks)
        return dread(u->disk, (uint32_t)lba, count, buf) ? -1 : 0;
    for (i = 0; i < CL_N; ++i)
        if (CL[i].valid && CL[i].disk == u->disk && CL[i].lba == base)
        { CL[i].used = ++CLClock; memcpy(buf, CLData[i] + ((uint32_t)lba - base) * d->bsize, (size_t)count * d->bsize); return 0; }
    for (i = 0; i < CL_N; ++i) if (v < 0 || !CL[i].valid || CL[i].used < CL[v].used) { v = i; if (!CL[i].valid) break; }
    CL[v].valid = 0;
    if (dread(u->disk, base, (int)spl, CLData[v])) return -1;
    CL[v].disk = u->disk; CL[v].lba = base; CL[v].valid = 1; CL[v].used = ++CLClock;
    memcpy(buf, CLData[v] + ((uint32_t)lba - base) * d->bsize, (size_t)count * d->bsize);
    return 0;
}

static uint32_t g32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t crc32(const uint8_t* p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFF; int k;
    while (n--) { c ^= *p++; for (k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320 & -(c & 1)); }
    return ~c;
}

/* ---- partitions: start blocks of the exFAT / NTFS ones on a disk ---- */
static int is_xn(int disk, uint32_t lba)
{
    return !dread(disk, lba, 1, Blk) && (!memcmp(Blk + 3, "EXFAT   ", 8) || !memcmp(Blk + 3, "NTFS    ", 8));
}

static int find_parts(int disk, uint32_t* out, int max)
{
    uint8_t mbr[512];
    int n = 0, i, gpt = 0;
    const USBPAD_Disk* d = USBPAD_DiskInfo(disk);
    if (!d || !d->ready || d->bsize < 512 || d->bsize > 4096) return 0;
    if (is_xn(disk, 0)) { out[0] = 0; return 1; }                      /* superfloppy */
    if (dread(disk, 0, 1, Blk)) return 0;
    memcpy(mbr, Blk, 512);
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) return 0;
    for (i = 0; i < 4; ++i) if (mbr[446 + i * 16 + 4] == 0xEE) gpt = 1;
    if (gpt)
    {
        uint32_t elba, en, es, k, hs, crc, per;
        if (dread(disk, 1, 1, Blk) || memcmp(Blk, "EFI PART", 8)) return 0;
        hs = g32(Blk + 12); crc = g32(Blk + 16);
        if (hs < 92 || hs > 512) return 0;
        memset(Blk + 16, 0, 4);
        if (crc32(Blk, hs) != crc) return 0;
        elba = g32(Blk + 72); en = g32(Blk + 80); es = g32(Blk + 84);
        if (es < 128 || es > d->bsize || (es & (es - 1))) return 0;
        if (en > 256) en = 256;
        per = d->bsize / es;
        for (k = 0; k < en && n < max; ++k)
        {
            uint8_t e[128]; uint32_t first, last; int z, j;
            if (dread(disk, elba + k / per, 1, Blk)) break;
            memcpy(e, Blk + (k % per) * es, 128);
            for (z = 1, j = 0; j < 16; ++j) if (e[j]) z = 0;
            first = g32(e + 32); last = g32(e + 40);
            if (z || g32(e + 36) || g32(e + 44) || last < first) continue;
            if (is_xn(disk, first)) out[n++] = first;
        }
        return n;
    }
    for (i = 0; i < 4 && n < max; ++i)
    {
        const uint8_t* e = mbr + 446 + i * 16;
        if (e[4] && e[4] != 0x05 && e[4] != 0x0F && e[4] != 0x85 && is_xn(disk, g32(e + 8))) out[n++] = g32(e + 8);
    }
    for (i = 0; i < 4; ++i)
    {
        const uint8_t* e = mbr + 446 + i * 16;
        if (e[4] == 0x05 || e[4] == 0x0F || e[4] == 0x85)
        {
            uint32_t ext = g32(e + 8), ebr = ext;
            int guard = 0;
            uint8_t b[64];
            while (n < max && guard++ < 64)
            {
                if (dread(disk, ebr, 1, Blk) || Blk[510] != 0x55 || Blk[511] != 0xAA) break;
                memcpy(b, Blk + 446, 32);
                if (b[4] && is_xn(disk, ebr + g32(b + 8))) out[n++] = ebr + g32(b + 8);
                if (!g32(b + 16 + 8) || (b[16 + 4] != 0x05 && b[16 + 4] != 0x0F && b[16 + 4] != 0x85)) break;
                ebr = ext + g32(b + 16 + 8);
            }
        }
    }
    return n;
}

static uint32_t boot_sig(RUnit* u) { return dread(u->disk, u->start, 1, Blk) ? 0 : crc32(Blk, 512) | 1; }

/* the unit's stick there and mounted? (follows the stick by gen + lun, re-mounts a new medium) */
static int ucheck(RUnit* u, int probe)
{
    const USBPAD_Disk* d = NULL;
    int n, nd = USBPAD_Disks(), k;
    uint32_t now = DPMI_LoadD(0x46C);
    for (n = 0; n < nd; ++n)
        if ((d = USBPAD_DiskInfo(n)) && d->gen == u->gen && d->lun == u->lun) break;
    if (n < nd) { if (n != u->disk) { cache_drop(u->disk); cache_drop(n); u->disk = n; } }
    else
    {   /* gone: a stick back in any port that no drive owns and that has this partition = a new medium for every
         * drive that was on the old stick */
        uint32_t og = u->gen; int ol = u->lun;
        d = NULL;
        for (n = 0; n < nd && !d; ++n)
        {
            const USBPAD_Disk* c = USBPAD_DiskInfo(n);
            uint32_t p[UR_UNITS];
            if (!c || !c->ready) continue;
            for (k = 0; k < NR && !(RU[k]->gen == c->gen && RU[k]->lun == c->lun); ++k) {}
            if (k < NR) continue;
            if (find_parts(n, p, UR_UNITS) > u->idx) { d = c; break; }
        }
        if (!d) { u->ok = 0; return 0; }
        for (k = 0; k < NR; ++k)
            if (RU[k]->gen == og && RU[k]->lun == ol) { RU[k]->gen = d->gen; RU[k]->lun = d->lun; RU[k]->disk = n; RU[k]->ok = 0; }
        cache_drop(n);
    }
    if (!d->ready) { u->ok = 0; return 0; }
    if (u->ok && probe && now - u->last >= 37)
    {   /* idle 2 s: a card swapped in a reader keeps the gen */
        cache_drop(u->disk);
        if (boot_sig(u) != u->sig) u->ok = 0;
    }
    if (!u->ok)
    {
        uint32_t p[UR_UNITS];
        cache_drop(u->disk);
        if (find_parts(u->disk, p, UR_UNITS) > u->idx)
        {
            u->start = p[u->idx];
            if (!fsr_mount(&u->v, rcb, u, u->start, u->work, sizeof u->work)) { u->ok = 1; u->mgen++; u->freeok = 0; u->sig = boot_sig(u); }
        }
    }
    u->last = now;
    return u->ok;
}

/* ---- DOS side ---- */
#define SDA_DTA  0x0C
#define SDA_FN1  0x9E
#define SDA_SDB  0x19E
#define SDA_DIR  0x1B3
#define SDA_ATTR 0x24D
#define SDA_CDS  0x282
#define SDA_XACT 0x2DD
#define SDA_XMOD 0x2E1

static RUnit* unit_of(int drive) { int i; for (i = 0; i < NR; ++i) if (RU[i]->drive == drive) return RU[i]; return NULL; }
static int unit_no(RUnit* u) { int i; for (i = 0; i < NR; ++i) if (RU[i] == u) return i; return -1; }

/* SDA first filename "X:\PATH" -> unit + path ("\PATH") in p */
static RUnit* fn_unit(uint32_t off, char* p, int max)
{
    int i;
    char c = (char)DPMI_LoadB(Sda + off);
    for (i = 0; i < max - 1 && (p[i] = (char)DPMI_LoadB(Sda + off + 2 + i)) != 0; ++i) {}
    p[i] = 0;
    return unit_of(c - 'A');
}

static void fcbname(const char* n, uint8_t* f)
{
    int i = 0;
    memset(f, ' ', 11);
    if (n[0] == '.') { f[0] = '.'; if (n[1] == '.') f[1] = '.'; return; }
    while (*n && *n != '.') { if (i < 8) f[i++] = (uint8_t)*n; n++; }
    if (*n == '.') for (n++, i = 8; *n && i < 11; n++) f[i++] = (uint8_t)*n;
}

static int fmatch(const char* pat, const uint8_t* f) { int i; for (i = 0; i < 11; ++i) if (pat[i] != '?' && (uint8_t)pat[i] != f[i]) return 0; return 1; }

static uint16_t ferr(int r)
{
    if (r == FSR_ENOFILE || r == FSR_ENOPATH || r == FSR_EACCES || r == FSR_ENOMEM || r == FSR_ENOMORE) return (uint16_t)-r;
    return 0x1F;                                                /* general failure */
}

static uint32_t clamp32(uint64_t x) { return x > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)x; }

static RFile* file_of(uint32_t sft)
{
    uint16_t s = DPMI_LoadW(sft + 0x0B);
    RFile* f;
    if (s >= UR_FILES) return NULL;
    f = &FL[s];
    if (!f->used || f->id != DPMI_LoadD(sft + 0x1B) || !ucheck(RU[f->unit], 0) || RU[f->unit]->mgen != f->mgen) return NULL;
    return f;
}

static uint16_t do_open(uint32_t sft, uint16_t mode, int* opened)
{
    char p[128]; fsr_dirent d; fsr_file f; int r, s; uint8_t fcb[11];
    RUnit* u = fn_unit(SDA_FN1, p, sizeof p);
    *opened = 0;
    if (!u || !ucheck(u, 1)) return 0x15;
    if ((r = fsr_open(&u->v, p, &f, &d)) != 0) return ferr(r);
    *opened = 1;
    if (d.attr & FSR_ATTR_DIR) return 0x05;
    for (s = 0; s < UR_FILES && FL[s].used; ++s) {}
    if (s == UR_FILES) return 0x04;                             /* too many open files */
    FL[s].used = 1; FL[s].unit = unit_no(u); FL[s].mgen = u->mgen; FL[s].id = ++Ids; FL[s].f = f;
    DPMI_StoreW(sft + 0x02, mode);
    DPMI_StoreB(sft + 0x04, d.attr);
    DPMI_StoreW(sft + 0x05, (uint16_t)(0x8040 | u->drive));
    DPMI_StoreD(sft + 0x07, 0);
    DPMI_StoreW(sft + 0x0B, (uint16_t)s);
    DPMI_StoreW(sft + 0x0D, d.time); DPMI_StoreW(sft + 0x0F, d.date);
    DPMI_StoreD(sft + 0x11, clamp32(d.size));
    DPMI_StoreD(sft + 0x15, 0);
    DPMI_StoreW(sft + 0x19, 0); DPMI_StoreD(sft + 0x1B, FL[s].id); DPMI_StoreB(sft + 0x1F, 0);
    fcbname(d.name, fcb);
    DPMI_LMemcpy(sft + 0x20, DPMI_PTR2L(fcb), 11);
    return 0;
}

static uint16_t do_read(uint32_t sft)
{
    RFile* f = file_of(sft);
    uint32_t dta = DPMI_LoadD(Sda + SDA_DTA), dst = DPMI_SEGOFF2L(dta >> 16, dta & 0xFFFF);
    uint32_t want = Reg.w.cx, done = 0;
    if (!f) return 0x06;
    f->f.pos = DPMI_LoadD(sft + 0x15);
    while (done < want)
    {
        uint32_t n = want - done;
        int32_t g;
        if (n > sizeof Bounce) n = sizeof Bounce;
        g = fsr_read(&RU[f->unit]->v, &f->f, Bounce, n);
        if (g < 0) { Reg.w.cx = (uint16_t)done; return ferr(g); }
        DPMI_LMemcpy(dst + done, DPMI_PTR2L(Bounce), (uint32_t)g);
        done += (uint32_t)g;
        if ((uint32_t)g < n) break;
    }
    DPMI_StoreD(sft + 0x15, DPMI_LoadD(sft + 0x15) + done);
    Reg.w.cx = (uint16_t)done;
    return 0;
}

/* one search result -> SDA search block + found entry, and the DTA (DOS copies one into the other, either way) */
static void put_found(RFind* s, int slot, int drive, const uint8_t* fcb, uint8_t attr, uint16_t time, uint16_t date, uint32_t size)
{
    uint8_t b[43]; uint32_t dta = DPMI_LoadD(Sda + SDA_DTA), dl = DPMI_SEGOFF2L(dta >> 16, dta & 0xFFFF);
    int i, o;
    memset(b, 0, sizeof b);
    b[0] = (uint8_t)(0x80 | drive);
    memcpy(b + 1, s->s.pat, 11);
    b[12] = s->s.amask;
    b[13] = (uint8_t)slot; b[14] = (uint8_t)(slot >> 8);
    b[15] = MAGIC & 0xFF; b[16] = MAGIC >> 8;
    memcpy(b + 17, &s->id, 4);
    b[21] = attr; memcpy(b + 22, &time, 2); memcpy(b + 24, &date, 2); memcpy(b + 26, &size, 4);
    for (i = 0, o = 30; i < 11; ++i)
    {
        if (i == 8 && fcb[8] != ' ') b[o++] = '.';
        if (fcb[i] != ' ') b[o++] = fcb[i];
    }
    DPMI_LMemcpy(Sda + SDA_SDB, DPMI_PTR2L(b), 21);
    DPMI_LMemcpy(dl, DPMI_PTR2L(b), 43);
    memset(b, 0, 32);
    memcpy(b, fcb, 11); b[11] = attr; memcpy(b + 22, &time, 2); memcpy(b + 24, &date, 2); memcpy(b + 28, &size, 4);
    DPMI_LMemcpy(Sda + SDA_DIR, DPMI_PTR2L(b), 32);
}

static uint16_t find_step(int slot)
{
    RFind* s = &FD[slot];
    RUnit* u = RU[s->unit];
    fsr_dirent d; uint8_t fcb[11]; int r;
    if (!ucheck(u, 0) || u->mgen != s->mgen) { s->used = 0; return 0x12; }
    s->age = ++Age;
    while (s->phase < 2)
    {   /* "." and ".." first in a sub-directory */
        const char* dn = s->phase ? ".." : ".";
        s->phase++;
        fcbname(dn, fcb);
        if ((s->s.amask & FSR_ATTR_DIR) && fmatch(s->s.pat, fcb))
        { put_found(s, slot, u->drive, fcb, FSR_ATTR_DIR, s->time, s->date, 0); return 0; }
    }
    if (s->phase == 2)
    {   /* the first match, kept back by findfirst */
        s->phase = 3;
        if (!s->pend) { s->used = 0; return 0x12; }
        d = s->pd;
    }
    else if ((r = fsr_findnext(&u->v, &s->s, &d)) != 0) { s->used = 0; return r == FSR_ENOMORE ? 0x12 : ferr(r); }
    fcbname(d.name, fcb);
    put_found(s, slot, u->drive, fcb, d.attr, d.time, d.date, clamp32(d.size));
    return 0;
}

static uint16_t do_findfirst(void)
{
    char p[128], dir[128], fcb[11]; fsr_dirent dd; fsr_file fd;
    RUnit* u = fn_unit(SDA_FN1, p, sizeof p);
    int i, slot = 0, last = 0, r;
    uint8_t amask = DPMI_LoadB(Sda + SDA_ATTR);
    RFind* s;
    if (!u || !ucheck(u, 1)) return 0x15;
    if (amask == 0x08) return 0x12;                             /* volume label only: none */
    for (i = 0; p[i]; ++i) if (p[i] == '\\') last = i;
    memcpy(dir, p, (size_t)last); dir[last] = 0;
    if (!last) strcpy(dir, "\\");
    if (fsr_open(&u->v, dir, &fd, &dd) || !(dd.attr & FSR_ATTR_DIR)) return 0x03;
    for (i = 0; i < UR_FINDS; ++i) { if (!FD[i].used) { slot = i; break; } if (FD[i].age < FD[slot].age) slot = i; }
    s = &FD[slot];
    memset(s, 0, sizeof *s);
    s->used = 1; s->unit = unit_no(u); s->mgen = u->mgen; s->id = ++Ids;
    s->phase = last ? 0 : 2; s->date = dd.date; s->time = dd.time;
    fsr_pattern(p + last + 1, fcb);
    r = fsr_findfirst(&u->v, dir, fcb, amask, &s->s, &s->pd);
    if (r && r != FSR_ENOMORE) { s->used = 0; return ferr(r); }
    s->pend = !r;
    return find_step(slot);
}

static uint16_t do_findnext(void)
{
    uint32_t dta = DPMI_LoadD(Sda + SDA_DTA), dl = DPMI_SEGOFF2L(dta >> 16, dta & 0xFFFF);
    uint16_t slot = DPMI_LoadW(dl + 13);
    if (slot >= UR_FINDS || !FD[slot].used || FD[slot].id != DPMI_LoadD(dl + 17)) return 0x12;
    return find_step(slot);
}

static uint16_t do_getattr(void)
{
    char p[128]; fsr_dirent d; fsr_file f; int r;
    RUnit* u = fn_unit(SDA_FN1, p, sizeof p);
    if (!u || !ucheck(u, 1)) return 0x15;
    if ((r = fsr_open(&u->v, p, &f, &d)) != 0) return ferr(r);
    Reg.w.ax = d.attr;
    Reg.w.bx = (uint16_t)(clamp32(d.size) >> 16); Reg.w.di = (uint16_t)clamp32(d.size);
    Reg.w.cx = d.time; Reg.w.dx = d.date;
    return 0;
}

static uint16_t do_chdir(void)
{
    char p[128]; fsr_dirent d; fsr_file f; int i;
    RUnit* u = fn_unit(SDA_FN1, p, sizeof p);
    uint32_t cp = DPMI_LoadD(Sda + SDA_CDS), cds = DPMI_SEGOFF2L(cp >> 16, cp & 0xFFFF);
    if (!u || !ucheck(u, 1)) return 0x15;
    if (p[0] && p[1] && (fsr_open(&u->v, p, &f, &d) || !(d.attr & FSR_ATTR_DIR))) return 0x03;
    for (i = 0; i < 66 && DPMI_LoadB(Sda + SDA_FN1 + i); ++i) {}
    if (i >= 66) return 0x03;
    if (DPMI_LoadB(cds) == 'A' + u->drive)                      /* DOS may do this itself too */
        DPMI_LMemcpy(cds, Sda + SDA_FN1, (uint32_t)i + 1);
    return 0;
}

static uint16_t do_space(void)
{
    RUnit* u = unit_of(DPMI_LoadB(DPMI_SEGOFF2L(Reg.w.es, Reg.w.di)) - 'A');
    uint64_t tot, fr; uint32_t spc;
    if (!u || !ucheck(u, 0)) return 0x15;
    if (!u->freeok && !fsr_freeclusters(&u->v, &u->nfree)) u->freeok = 1;
    tot = u->v.nclus; fr = u->freeok ? u->nfree : 0;
    spc = u->v.cs / 512;                                        /* in 512-byte sectors, at most 64 per cluster */
    while (spc > 64) { spc /= 2; tot *= 2; fr *= 2; }
    while (tot > 0xFFFF && spc < 64) { spc *= 2; tot /= 2; fr /= 2; }
    if (tot > 0xFFFF) { fr = fr * 0xFFFF / tot; tot = 0xFFFF; }  /* DOS can show 2 GB at most */
    Reg.w.ax = (uint16_t)spc;                                  /* AH (media) 0: FreeDOS 7303h takes AX whole */ Reg.w.bx = (uint16_t)tot; Reg.w.cx = 512; Reg.w.dx = (uint16_t)fr;
    return 0;
}

static void UR_Req(void)
{
    uint32_t sft = DPMI_SEGOFF2L(Reg.w.es, Reg.w.di);
    uint16_t e = 0;
    int opened;
    __asm__ __volatile__("sti");
    switch (Reg.h.al)
    {
    case 0x01: case 0x03: case 0x09: case 0x0E: case 0x11: case 0x13: case 0x17: e = 0x05; break;   /* read-only */
    case 0x05: e = do_chdir(); break;
    case 0x06:                                                  /* close: the redirector drops the handle count */
    {
        uint16_t c = DPMI_LoadW(sft);
        RFile* f = file_of(sft);
        if (c) DPMI_StoreW(sft, --c);
        if (!c && f) f->used = 0;
        break;
    }
    case 0x07: case 0x0A: case 0x0B: break;                     /* commit, lock, unlock */
    case 0x08: e = do_read(sft); break;
    case 0x0C: e = do_space(); break;
    case 0x0F: e = do_getattr(); break;
    case 0x16:                                                  /* open; mode on the caller's stack */
        e = do_open(sft, DPMI_LoadW(DPMI_SEGOFF2L(Reg.w.ss, (uint16_t)(Reg.w.sp + 10))), &opened);
        break;
    case 0x1B: e = do_findfirst(); break;
    case 0x1C: e = do_findnext(); break;
    case 0x21:                                                  /* seek from end: CX:DX signed */
    {
        RFile* f = file_of(sft);
        int64_t np;
        if (!f) { e = 0x06; break; }
        np = (int64_t)f->f.size + (int32_t)((uint32_t)Reg.w.cx << 16 | Reg.w.dx);
        if (np < 0) np = 0;
        DPMI_StoreD(sft + 0x15, clamp32((uint64_t)np));
        Reg.w.dx = (uint16_t)(clamp32((uint64_t)np) >> 16); Reg.w.ax = (uint16_t)clamp32((uint64_t)np);
        break;
    }
    case 0x2E:                                                  /* extended open (INT 21h 6C00h) */
    {
        uint16_t act = DPMI_LoadW(Sda + SDA_XACT), mode = DPMI_LoadW(Sda + SDA_XMOD);
        e = do_open(sft, mode & 0x7F, &opened);
        if (opened) { if (!e && (act & 0x0F) != 1) e = (act & 0x0F) ? 0x05 : 0x50; }  /* replace / fail if exists */
        else if (e == 0x02 && (act & 0xF0)) e = 0x05;           /* create */
        if (!e) Reg.w.cx = 1;                                   /* opened */
        else if (opened && DPMI_LoadW(sft + 0x0B) < UR_FILES && FL[DPMI_LoadW(sft + 0x0B)].id == DPMI_LoadD(sft + 0x1B))
            FL[DPMI_LoadW(sft + 0x0B)].used = 0;
        break;
    }
    default: e = 0x01;                                          /* function not supported */
    }
    if (e) { Reg.w.flags |= 1; Reg.w.ax = e; } else Reg.w.flags &= ~1;
    __asm__ __volatile__("cli");
}

/* ---- install ---- */
static int dos_call(DPMI_REG* r) { return DPMI_CallRealModeINT(0x21, r) == 0; }
static uint32_t base_lin(void) { return DPMI_SEGOFF2L(Seg, 0); }

int USBREDIR_Install(void)
{
    static const uint8_t cls[0x30] = {
        /*00*/ 0,1,0,1,0,1,2,2, 2,2,2,2,3,0,1,1, /*10*/ 0,1,0,1,0,0,1,1, 0,0,0,1,4,0,0,0,
        /*20*/ 0,2,0,0,0,0,0,0, 0,0,0,0,0,0,1,0 };
    DPMI_REG r;
    uint32_t lol, mask = 0, old, sdafar;
    uint8_t stub[UR_STUB];
    int disks = USBPAD_Disks(), d, i, lastdrive, nblk, drive;
    Letters[0] = 0;
    if (NR) return NR;
    if (!disks) { strcpy(Msg, "no USB disk"); return 0; }
    for (d = 0; d < disks && NR < UR_UNITS; ++d)
    {
        uint32_t p[UR_UNITS];
        const USBPAD_Disk* di = USBPAD_DiskInfo(d);
        int n = find_parts(d, p, UR_UNITS);
        for (i = 0; i < n && NR < UR_UNITS; ++i)
        {
            RUnit* u = (RUnit*)calloc(1, sizeof(RUnit));
            if (!u) break;
            u->disk = d; u->idx = i; u->start = p[i]; u->gen = di ? di->gen : 0; u->lun = di ? di->lun : 0;
            if (fsr_mount(&u->v, rcb, u, u->start, u->work, sizeof u->work)) { free(u); continue; }
            u->ok = 1; u->mgen = 1; u->sig = boot_sig(u); u->last = DPMI_LoadD(0x46C);
            RU[NR++] = u;
        }
    }
    if (!NR) { strcpy(Msg, "no exFAT / NTFS partition on the USB disk(s)"); return 0; }

    memset(&r, 0, sizeof r); r.w.ax = 0x5D06; dos_call(&r);
    Sda = DPMI_SEGOFF2L(r.w.ds, r.w.si); sdafar = (uint32_t)r.w.ds << 16 | r.w.si;
    memset(&r, 0, sizeof r); r.h.ah = 0x52; dos_call(&r);
    lol = DPMI_SEGOFF2L(r.w.es, r.w.bx);
    CdsBase = DPMI_SEGOFF2L(DPMI_LoadW(lol + 0x18), DPMI_LoadW(lol + 0x16));
    nblk = DPMI_LoadB(lol + 0x20); lastdrive = DPMI_LoadB(lol + 0x21);
    for (i = 0, drive = nblk < 2 ? 2 : nblk; i < NR; ++i, ++drive)
    {
        while (drive < lastdrive && (DPMI_LoadW(CdsBase + drive * UR_CDS + 0x43) & 0xC000)) ++drive;
        if (drive >= lastdrive || drive > 31) break;
        RU[i]->drive = drive; mask |= 1u << drive;
    }
    if (i < NR) { while (NR > i) free(RU[--NR]); }
    if (!NR) { sprintf(Msg, "no free drive letter (LASTDRIVE=%c)", 'A' + lastdrive - 1); return 0; }

    Seg = (uint16_t)DPMI_HighMalloc((UR_STUB + 15) / 16, TRUE);
    Cb = DPMI_AllocateRMCB_RETF(&UR_Req, &Reg);
    if (!Seg || !Cb) { while (NR) free(RU[--NR]); strcpy(Msg, "no memory for the redirector stub"); return 0; }
    typedef char stub_fits[sizeof USBREDIR_Stub <= UR_STUB ? 1 : -1]; (void)sizeof(stub_fits);
    memset(stub, 0, sizeof stub);
    memcpy(stub, USBREDIR_Stub, sizeof USBREDIR_Stub);
    memcpy(stub + 0x04, &Cb, 4);
    memcpy(stub + 0x08, &sdafar, 4);
    memcpy(stub + 0x0C, &mask, 4);
    memcpy(stub + 0x10, cls, sizeof cls);

    __asm__ __volatile__("cli");
    old = DPMI_LoadD(0x2F * 4);
    memcpy(stub, &old, 4);
    DPMI_LMemcpy(base_lin(), DPMI_PTR2L(stub), sizeof stub);
    for (i = 0; i < NR; ++i)
    {
        uint32_t c = CdsBase + RU[i]->drive * UR_CDS;
        DPMI_LMemset(c, 0, UR_CDS);
        DPMI_StoreB(c + 0, (uint8_t)('A' + RU[i]->drive)); DPMI_StoreB(c + 1, ':'); DPMI_StoreB(c + 2, '\\');
        DPMI_StoreD(c + 0x45, 0);                               /* no DPB */
        DPMI_StoreW(c + 0x4F, 2);                               /* backslash offset */
        DPMI_StoreW(c + 0x43, 0xC000);                          /* network + physical */
    }
    DPMI_StoreD(0x2F * 4, (uint32_t)Seg << 16 | 0x40);
    __asm__ __volatile__("sti");

    for (i = 0; i < NR; ++i) sprintf(Letters + strlen(Letters), "%s%c:", i ? " " : "", 'A' + RU[i]->drive);
    for (i = 0; i < NR; ++i)
    {
        const USBPAD_Disk* di = USBPAD_DiskInfo(RU[i]->disk);
        char nm[40];
        snprintf(nm, sizeof nm, "%s %s", di ? di->vendor : "", di ? di->product : "");
        USBDISK_RedirNote(i, RU[i]->drive, RU[i]->v.type == FSR_NTFS ? 2 : 1, nm);
    }
    sprintf(Msg, "%d drive(s) %s, first %s %lu MB, read-only", NR, Letters, RU[0]->v.type == FSR_NTFS ? "NTFS" : "exFAT",
        (unsigned long)((RU[0]->v.nclus * RU[0]->v.cs) >> 20));
    return NR;
}

const char* USBREDIR_Letters(void) { return Letters; }
const char* USBREDIR_Msg(void) { return Msg; }

void USBREDIR_Uninstall(void)
{
    int i;
    if (!NR || !Seg) return;
    __asm__ __volatile__("cli");
    DPMI_StoreD(base_lin() + 0x04, 0);                          /* stub: chain everything */
    for (i = 0; i < NR; ++i) DPMI_StoreW(CdsBase + RU[i]->drive * UR_CDS + 0x43, 0);
    if (DPMI_LoadD(0x2F * 4) == ((uint32_t)Seg << 16 | 0x40)) DPMI_StoreD(0x2F * 4, DPMI_LoadD(base_lin()));
    __asm__ __volatile__("sti");
    NR = 0;
}
