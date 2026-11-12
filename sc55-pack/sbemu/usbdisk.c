/* USBDISK - USB disks as DOS drive letters, see usbdisk.h and docs/USB-STORAGE.md 5a / 6.
 *
 * SBEMU goes resident from AUTOEXEC, after DOS has built its drive list, so this does what DEVLOAD does: a small
 * real-mode block device (usbdisk.asm, attribute 0802h: removable + 32-bit sectors) whose interrupt routine
 * far-calls a DPMI real-mode callback (USBDISK_Req, private stack from DJGPP's RMCB wrapper). The device header
 * goes into the device chain after NUL, one DPB per partition (INT 21h AH=53h from the boot sector's BPB) at the
 * end of the DPB chain, and a CDS entry per drive (first free letter after the block devices, up to LASTDRIVE).
 * DOS does the FAT itself: FAT12/16 everywhere, FAT32 on DOS 7.10+ / FreeDOS.
 * Partitions: MBR primaries + logicals in extended chains, GPT basic data (header CRC checked), superfloppy.
 * DOS sectors are 512 bytes: disks with other block sizes are skipped (TODO). Writes: USBPAD_DiskWrite, then the
 * same sectors in the cache are updated (write-through); a write-protected disk answers "write protected".
 * Cache: 16 lines x 4 KB (8 sectors, aligned) for the one-sector reads DOS does for FAT / directories;
 * multi-sector reads go straight through a 32 KB bounce buffer. Dropped on a medium change. GPL-2.0-or-later. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "dpmi/dpmi.h"
#include "usbpad.h"
#include "usbdisk.h"
#include "usbdiskbin.h"

#define UD_UNITS 8
#define UD_SPARE 2              /* drives kept for sticks plugged in after SBEMU starts */
#define UD_STUB  0x80          /* stub code size rounded up (usbdisk.asm) */
#define UD_BPB   0x40          /* per unit: BPB copy for BUILD BPB (boot sector 0Bh..4Ah) */
#define UD_DPB   0x40          /* per unit: DPB (DOS 7 extended DPB is 3Dh bytes) */
#define UD_CDS   0x58
#define UD_INFO  (UD_STUB + UD_UNITS * (UD_BPB + UD_DPB))   /* per unit 48 bytes for USBDRV: state, FAT bits, name */
#define UD_INFOSZ 48
#define UD_RINFO (UD_INFO + UD_UNITS * UD_INFOSZ)   /* 8 x 48 bytes: exFAT / NTFS drives of usbredir.c, for USBDRV */

typedef struct
{
    int disk;                  /* USBPAD disk number (shifts when an earlier disk goes: see unit_disk) */
    int lun;                   /* card reader slot; with gen: which stick the drive belongs to */
    int part;                  /* which FAT partition on that disk (0..) */
    int fat;                   /* 12 / 16 / 32 */
    uint32_t start, size;      /* partition on the disk */
    uint32_t gen;              /* USBPAD gen when the partition was found */
    int changed;               /* a new medium seen: next MEDIA CHECK says "changed" */
    int ok;                    /* partition there now */
    uint32_t last;             /* BIOS tick of the last access */
    uint32_t sig;              /* CRC of LBA 0 + the partition's boot sector: tells a swapped card (same gen) */
    int drive;                 /* 0 = A: */
} Unit;

static Unit U[UD_UNITS];
static int NU;
static uint16_t Seg;           /* stub + BPBs + DPBs */
static uint32_t Cb;
static DPMI_REG Reg;
static uint32_t CdsBase;
static int Fat32Ok;
static char Letters[3 * UD_UNITS + 1], Msg[120];
static uint8_t Bounce[32768];
static uint8_t Sec[512];

/* ---- sector cache: 16 lines of 8 aligned sectors (device LBA) ---- */
#define CL_N 16
#define CL_S 8
static struct { int disk; uint32_t lba; uint32_t used; int valid; } CL[CL_N];
static uint8_t CLData[CL_N][CL_S * 512];
static uint32_t CLClock;

static void cache_drop(int disk)
{
    int i;
    for (i = 0; i < CL_N; ++i) if (CL[i].disk == disk) CL[i].valid = 0;
}

/* raw read with a retry while usbpad is busy (an enumeration from the interrupt); 0 or USBPAD_E_* */
static int dread(int disk, uint32_t lba, int n, void* buf)
{
    int r, t;
    for (t = 0; t < 200; ++t)
    {
        r = USBPAD_DiskRead(disk, lba, n, buf);
        if (r != USBPAD_E_BUSY) return r;
        { uint32_t t0 = DPMI_LoadD(0x46C); while (DPMI_LoadD(0x46C) == t0) {} }   /* ~55 ms */
    }
    return r;
}

static int dwrite(int disk, uint32_t lba, int n, const void* buf)
{
    int r, t;
    for (t = 0; t < 200; ++t)
    {
        r = USBPAD_DiskWrite(disk, lba, n, buf);
        if (r != USBPAD_E_BUSY) return r;
        { uint32_t t0 = DPMI_LoadD(0x46C); while (DPMI_LoadD(0x46C) == t0) {} }   /* ~55 ms */
    }
    return r;
}

static int cread(int disk, uint32_t lba, uint8_t* out)
{
    uint32_t base = lba & ~(uint32_t)(CL_S - 1);
    int i, v = -1, r;
    const USBPAD_Disk* d = USBPAD_DiskInfo(disk);
    for (i = 0; i < CL_N; ++i)
        if (CL[i].valid && CL[i].disk == disk && CL[i].lba == base) { CL[i].used = ++CLClock; memcpy(out, CLData[i] + (lba - base) * 512, 512); return 0; }
    if (!d) return USBPAD_E_NODISK;
    for (i = 0; i < CL_N; ++i) if (v < 0 || !CL[i].valid || CL[i].used < CL[v].used) { v = i; if (!CL[i].valid) break; }
    if (base + CL_S > d->blocks) return dread(disk, lba, 1, out);     /* last few sectors: uncached */
    CL[v].valid = 0;
    if ((r = dread(disk, base, CL_S, CLData[v])) != 0) return r;
    CL[v].disk = disk; CL[v].lba = base; CL[v].valid = 1; CL[v].used = ++CLClock;
    memcpy(out, CLData[v] + (lba - base) * 512, 512);
    return 0;
}

/* ---- partitions ---- */
static uint16_t g16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t g32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static uint32_t crc32(const uint8_t* p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFF; int k;
    while (n--) { c ^= *p++; for (k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320 & -(c & 1)); }
    return ~c;
}

/* a FAT boot sector? 12/16/32, 0 = no */
static int fat_type(const uint8_t* b, uint32_t psize)
{
    uint16_t bps = g16(b + 11), res = g16(b + 14), rootn = g16(b + 17);
    uint8_t spc = b[13], nf = b[16];
    uint32_t tot = g16(b + 19) ? g16(b + 19) : g32(b + 32), fsz = g16(b + 22) ? g16(b + 22) : g32(b + 36), data, cl;
    if (b[510] != 0x55 || b[511] != 0xAA) { if (b[0] != 0xEB && b[0] != 0xE9) return 0; }
    if (bps != 512 || !spc || (spc & (spc - 1)) || !res || nf < 1 || nf > 2 || !tot || !fsz) return 0;
    if (psize && tot > psize) return 0;
    data = res + nf * fsz + (rootn * 32 + 511) / 512;
    if (data >= tot) return 0;
    cl = (tot - data) / spc;
    if (cl < 4085) return 12;
    if (cl < 65525) return g16(b + 22) ? 16 : 0;
    return (!g16(b + 22) && !rootn) ? 32 : 0;
}

typedef struct { uint32_t start, size; int fat; } Part;

static int add_part(int disk, uint32_t start, uint32_t size, Part* out, int n, int max)
{
    int t;
    if (n >= max || !size || dread(disk, start, 1, Sec)) return n;
    t = fat_type(Sec, size);
    if (t == 32 && !Fat32Ok) t = 0;
    if (t) { out[n].start = start; out[n].size = size; out[n].fat = t; ++n; }
    return n;
}

static int is_fat_mbr_type(uint8_t t) { return t == 0x01 || t == 0x04 || t == 0x06 || t == 0x0B || t == 0x0C || t == 0x0E; }

static int find_parts(int disk, Part* out, int max)
{
    uint8_t mbr[512];
    int n = 0, i, gpt = 0;
    const USBPAD_Disk* d = USBPAD_DiskInfo(disk);
    if (!d || !d->ready || d->bsize != 512) return 0;
    if (dread(disk, 0, 1, mbr)) return 0;
    if (fat_type(mbr, d->blocks)) return add_part(disk, 0, d->blocks, out, 0, max);     /* superfloppy */
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) return 0;
    for (i = 0; i < 4; ++i) if (mbr[446 + i * 16 + 4] == 0xEE) gpt = 1;
    if (gpt)
    {
        uint8_t h[512];
        static const uint8_t basic[16] = { 0xA2,0xA0,0xD0,0xEB,0xE5,0xB9,0x33,0x44,0x87,0xC0,0x68,0xB6,0xB7,0x26,0x99,0xC7 };
        uint32_t hs, crc, elba, en, es, k;
        if (dread(disk, 1, 1, h) || memcmp(h, "EFI PART", 8)) return 0;
        hs = g32(h + 12); crc = g32(h + 16);
        if (hs < 92 || hs > 512) return 0;
        memset(h + 16, 0, 4);
        if (crc32(h, hs) != crc) return 0;
        elba = g32(h + 72); en = g32(h + 80); es = g32(h + 84);
        if (es < 128 || es > 512 || (es & (es - 1))) return 0;
        if (en > 256) en = 256;
        for (k = 0; k < en && n < max; ++k)
        {
            uint32_t off = k * es, first, last;
            if (off % 512 == 0 && dread(disk, elba + off / 512, 1, h)) break;
            first = g32(h + off % 512 + 32); last = g32(h + off % 512 + 40);
            if (g32(h + off % 512 + 36) || g32(h + off % 512 + 44)) continue;      /* above 2 TB */
            if (!memcmp(h + off % 512, basic, 16) && last >= first) n = add_part(disk, first, last - first + 1, out, n, max);
        }
        return n;
    }
    for (i = 0; i < 4; ++i)
    {
        const uint8_t* e = mbr + 446 + i * 16;
        if (is_fat_mbr_type(e[4])) n = add_part(disk, g32(e + 8), g32(e + 12), out, n, max);
    }
    for (i = 0; i < 4; ++i)
    {
        const uint8_t* e = mbr + 446 + i * 16;
        if (e[4] == 0x05 || e[4] == 0x0F || e[4] == 0x85)
        {
            uint32_t ext = g32(e + 8), ebr = ext;
            int guard = 0;
            uint8_t b[512];
            while (n < max && guard++ < 64)
            {
                if (dread(disk, ebr, 1, b) || b[510] != 0x55 || b[511] != 0xAA) break;
                if (is_fat_mbr_type(b[446 + 4])) n = add_part(disk, ebr + g32(b + 446 + 8), g32(b + 446 + 12), out, n, max);
                if (!g32(b + 462 + 8) || (b[462 + 4] != 0x05 && b[462 + 4] != 0x0F && b[462 + 4] != 0x85)) break;
                ebr = ext + g32(b + 462 + 8);
            }
        }
    }
    return n;
}

static uint32_t base_lin(void) { return DPMI_SEGOFF2L(Seg, 0); }
static uint32_t bpb_lin(int u) { return base_lin() + UD_STUB + u * UD_BPB; }
static uint32_t dpb_lin(int u) { return base_lin() + UD_STUB + UD_UNITS * UD_BPB + u * UD_DPB; }

/* boot sector of unit u -> its BPB slot in the stub segment; 0 ok */
/* a FAT16 BPB for a drive with no stick yet (DOS needs one to build the DPB; the real one comes with BUILD BPB) */
static void spare_bpb(int u)
{
    memset(Sec, 0, 512);
    Sec[11] = 0x00; Sec[12] = 0x02;                             /* 512 bytes per sector */
    Sec[13] = 4; Sec[14] = 1; Sec[16] = 2;                      /* 4 per cluster, 1 reserved, 2 FATs */
    Sec[17] = 0x00; Sec[18] = 0x02;                             /* 512 root entries */
    Sec[21] = 0xF8; Sec[22] = 0x40;                             /* media, 64 sectors per FAT */
    Sec[32] = 0x00; Sec[33] = 0x00; Sec[34] = 0x01;            /* 65536 sectors (32 MB) */
    Sec[36] = 0x40;                                             /* extended BPB: FAT size again (see load_bpb) */
    Sec[48] = Sec[49] = Sec[50] = Sec[51] = 0xFF;
    DPMI_LMemcpy(bpb_lin(u), DPMI_PTR2L(Sec + 11), UD_BPB);
}

static int load_bpb(int u)
{
    if (dread(U[u].disk, U[u].start, 1, Sec)) return -1;
    if (fat_type(Sec, U[u].size) != U[u].fat) return -1;
    if (U[u].fat != 32)
    {   /* DOS 7 / FreeDOS get it as an extended BPB (AH=53h with CX/DX 'XE','AR'): FAT32 fields synthesised
         * (FAT size, no mirroring flags, root cluster 0, no FSINFO / backup), FAT16 drive / serial / label dropped */
        uint16_t fs = g16(Sec + 22);
        memset(Sec + 36, 0, 40);
        Sec[36] = (uint8_t)fs; Sec[37] = (uint8_t)(fs >> 8);
        Sec[48] = Sec[49] = Sec[50] = Sec[51] = 0xFF;
    }
    DPMI_LMemcpy(bpb_lin(u), DPMI_PTR2L(Sec + 11), UD_BPB);
    return 0;
}

/* usbpad numbers disks in table order, so unplugging the first stick renumbers the second: follow each drive's stick
 * by its plug-in gen (a global counter in usbpad, so two sticks of the same model differ) + LUN. A new stick in the
 * drive's number that no other drive owns counts as a new medium for the drive (and for its partner partitions). */
static int owned(const USBPAD_Disk* d)              /* some drive has this stick */
{
    int k;
    for (k = 0; k < NU; ++k) if (U[k].gen == d->gen && U[k].lun == d->lun) return 1;
    return 0;
}
static const USBPAD_Disk* unit_disk(int u)
{
    uint32_t old = U[u].gen;
    int lun = U[u].lun, from = U[u].disk, n, k, nd = USBPAD_Disks();
    const USBPAD_Disk* d;
    for (n = 0; n < nd; ++n)
        if ((d = USBPAD_DiskInfo(n)) && d->gen == old && d->lun == lun)
        {
            if (n != from)
            {
                cache_drop(from); cache_drop(n);
                for (k = 0; k < NU; ++k) if (U[k].gen == old && U[k].lun == lun) U[k].disk = n;
            }
            return d;
        }
    /* our stick is gone: a disk no drive owns becomes this drive's new medium - its old number first, then any (a
     * spare drive, made for sticks plugged in after SBEMU started, has none: disk -1) */
    n = -1;
    if (from >= 0 && (d = USBPAD_DiskInfo(from)) && !owned(d)) n = from;
    for (k = 0; n < 0 && k < nd; ++k) if ((d = USBPAD_DiskInfo(k)) && !owned(d)) n = k;
    if (n < 0 || !(d = USBPAD_DiskInfo(n))) return NULL;
    if (from >= 0) cache_drop(from);
    cache_drop(n);
    for (k = 0; k < NU; ++k)
        if (U[k].gen == old && U[k].lun == lun) { U[k].disk = n; U[k].gen = d->gen; U[k].lun = d->lun; U[k].ok = 0; U[k].changed = 1; }
    return d;
}

/* CRC of the disk's LBA 0 and the unit's boot sector, read past the cache; 0 = read failed */
static uint32_t unit_sig(int u)
{
    uint32_t c;
    if (dread(U[u].disk, 0, 1, Sec)) return 0;
    c = crc32(Sec, 512);
    if (dread(U[u].disk, U[u].start, 1, Sec)) return 0;
    return (c ^ crc32(Sec, 512)) | 1;
}

/* what USBDRV.EXE shows (stub segment + UD_INFO): [0] 0 no stick / 1 FAT drive / 2 stick without a usable FAT
 * partition, [1] 12/16/32, [2..3] 0, [4..] "vendor product" */
static void info_set(int u, int state)
{
    char b[UD_INFOSZ];
    const USBPAD_Disk* d = state && U[u].disk >= 0 ? USBPAD_DiskInfo(U[u].disk) : NULL;
    if (!Seg) return;
    memset(b, 0, sizeof b);
    b[0] = (char)state; b[1] = (char)(state == 1 ? U[u].fat : 0);
    if (d) snprintf(b + 4, sizeof b - 4, "%s %s", d->vendor, d->product);
    DPMI_LMemcpy(DPMI_SEGOFF2L(Seg, UD_INFO + u * UD_INFOSZ), DPMI_PTR2L(b), sizeof b);
}

/* medium check of unit u: same disk gen -> 1; new medium -> re-find the partition (0 if gone) */
static int unit_check_(int u);
static int unit_check(int u)
{
    int r = unit_check_(u);
    info_set(u, r ? 1 : U[u].disk >= 0 && USBPAD_DiskInfo(U[u].disk) && USBPAD_DiskInfo(U[u].disk)->ready ? 2 : 0);
    return r;
}
static int unit_check_(int u)
{
    const USBPAD_Disk* d = unit_disk(u);
    Part p[UD_UNITS];
    int n;
    if (!d || !d->ready) { if (U[u].ok) { U[u].ok = 0; U[u].changed = 1; cache_drop(U[u].disk); } return 0; }
    if (U[u].ok && d->gen == U[u].gen) return 1;
    cache_drop(U[u].disk);
    U[u].changed = 1; U[u].ok = 0; U[u].gen = d->gen;
    n = find_parts(U[u].disk, p, UD_UNITS);
    if (U[u].part >= n) return 0;
    U[u].start = p[U[u].part].start; U[u].size = p[U[u].part].size; U[u].fat = p[U[u].part].fat;
    if (load_bpb(u)) return 0;
    U[u].sig = unit_sig(u);
    U[u].ok = 1;
    return 1;
}

/* ---- the request handler (DPMI real-mode callback, RETF) ---- */
#define ST_DONE 0x0100
#define ST_ERR  0x8000
static void rq_status(uint32_t rq, uint16_t s) { DPMI_StoreW(rq + 3, s); }

static uint16_t rq_rw(uint32_t rq, int u, int write)
{
    uint32_t xfer = DPMI_SEGOFF2L(DPMI_LoadW(rq + 0x10), DPMI_LoadW(rq + 0x0E));
    uint32_t cnt = DPMI_LoadW(rq + 0x12), sec = DPMI_LoadW(rq + 0x14), done = 0;
    int r = 0;
    if (sec == 0xFFFF && DPMI_LoadB(rq) >= 0x1E) sec = DPMI_LoadD(rq + 0x1A);
    DPMI_StoreW(rq + 0x12, 0);
    if (!unit_check(u)) return ST_ERR | 0x02;                  /* not ready */
    if (U[u].changed && !write) { /* DOS reads without a MEDIA CHECK after a change: fine, the BPB is the same kind */ }
    if (write)
    {   /* write-through: the device first, then any cache line holding those sectors */
        const USBPAD_Disk* d = USBPAD_DiskInfo(U[u].disk);
        if (sec >= U[u].size || cnt > U[u].size - sec) return ST_ERR | 0x08;
        if (!d || d->wp) return ST_ERR | 0x00;                  /* write protected */
        U[u].last = DPMI_LoadD(0x46C);
        while (done < cnt && !r)
        {
            uint32_t n = cnt - done, lba = U[u].start + sec + done, k;
            int i;
            if (n > sizeof(Bounce) / 512) n = sizeof(Bounce) / 512;
            DPMI_LMemcpy(DPMI_PTR2L(Bounce), xfer + done * 512, n * 512);
            r = dwrite(U[u].disk, lba, (int)n, Bounce);
            if (r) break;
            for (i = 0; i < CL_N; ++i)
                if (CL[i].valid && CL[i].disk == U[u].disk)
                    for (k = 0; k < n; ++k)
                        if (lba + k >= CL[i].lba && lba + k < CL[i].lba + CL_S)
                            memcpy(CLData[i] + (lba + k - CL[i].lba) * 512, Bounce + k * 512, 512);
            done += n;
        }
        DPMI_StoreW(rq + 0x12, (uint16_t)done);
        if (r == USBPAD_E_WP) return ST_ERR | 0x00;
        if (r == USBPAD_E_NOTREADY || r == USBPAD_E_NODISK) { U[u].ok = 0; U[u].changed = 1; return ST_ERR | 0x02; }
        if (r) return ST_ERR | 0x0A;                             /* write fault */
        return 0;
    }
    if (sec >= U[u].size || cnt > U[u].size - sec) return ST_ERR | 0x08;
    U[u].last = DPMI_LoadD(0x46C);
    if (cnt == 1)
    {
        r = cread(U[u].disk, U[u].start + sec, Sec);
        if (!r) { DPMI_LMemcpy(xfer, DPMI_PTR2L(Sec), 512); done = 1; }
    }
    else while (done < cnt && !r)
    {
        uint32_t n = cnt - done;
        if (n > sizeof(Bounce) / 512) n = sizeof(Bounce) / 512;
        r = dread(U[u].disk, U[u].start + sec + done, (int)n, Bounce);
        if (!r) { DPMI_LMemcpy(xfer + done * 512, DPMI_PTR2L(Bounce), n * 512); done += n; }
    }
    DPMI_StoreW(rq + 0x12, (uint16_t)done);
    if (r == USBPAD_E_NOTREADY || r == USBPAD_E_NODISK) { U[u].ok = 0; U[u].changed = 1; return ST_ERR | 0x02; }
    if (r) return ST_ERR | 0x0B;                                 /* read fault */
    return 0;
}

static void USBDISK_Req(void)
{
    uint32_t rq = DPMI_SEGOFF2L(Reg.w.es, Reg.w.bx);
    int u = DPMI_LoadB(rq + 1), cmd = DPMI_LoadB(rq + 2);
    uint16_t st = 0;
    __asm__ __volatile__("sti");                                /* the sound keeps playing during a long read */
    if (u >= NU) { rq_status(rq, ST_DONE | ST_ERR | 0x01); __asm__ __volatile__("cli"); return; }
    switch (cmd)
    {
    case 0x01:  /* MEDIA CHECK: 1 not changed, 0 don't know, -1 changed (+0Fh volume id) */
    {
        int8_t m;
        if (!unit_check(u) || U[u].changed) m = -1;
        else if (DPMI_LoadD(0x46C) - U[u].last < 37) m = 1;
        else
        {   /* idle 2 s: a card reader's card may have been swapped without a re-plug (usbpad keeps the gen and
             * absorbs the unit attention) - compare LBA 0 + boot sector, and start the cache afresh anyway */
            uint32_t sg = unit_sig(u);
            cache_drop(U[u].disk);
            m = 0;
            if (sg != U[u].sig) { U[u].ok = 0; unit_check(u); m = -1; }   /* -1 also when the new card has no FAT */
            U[u].last = DPMI_LoadD(0x46C);
        }
        if (m == -1) U[u].changed = 0;
        DPMI_StoreB(rq + 0x0E, (uint8_t)m);
        DPMI_StoreW(rq + 0x0F, 0x1A); DPMI_StoreW(rq + 0x11, Seg);
        break;
    }
    case 0x02:  /* BUILD BPB -> +12h */
        if (!unit_check(u)) { st = ST_ERR | 0x02; break; }
        U[u].changed = 0;
        DPMI_StoreW(rq + 0x12, UD_STUB + u * UD_BPB); DPMI_StoreW(rq + 0x14, Seg);
        break;
    case 0x04: st = rq_rw(rq, u, 0); break;
    case 0x08: case 0x09: st = rq_rw(rq, u, 1); break;
    case 0x0D: case 0x0E: break;                                /* open / close */
    case 0x0F: break;                                           /* removable: busy bit clear = removable */
    default: st = ST_ERR | 0x03;                                /* unknown command */
    }
    rq_status(rq, ST_DONE | st);
    __asm__ __volatile__("cli");
}

/* ---- linking into DOS ---- */
static int dos_call(DPMI_REG* r) { return DPMI_CallRealModeINT(0x21, r) == 0; }

int USBDISK_Install(void)
{
    DPMI_REG r;
    uint32_t lol, p, last, nul;
    uint8_t stub[UD_STUB];
    int disks = USBPAD_Disks(), d, i, lastdrive, nblk, drive, nfound;
    Letters[0] = 0;
    if (NU) return NU;

    memset(&r, 0, sizeof r); r.w.ax = 0x3306; dos_call(&r);
    {
        int maj = r.h.bl, min = r.h.bh;
        memset(&r, 0, sizeof r); r.w.ax = 0x3000; dos_call(&r);
        if (r.h.al > maj || (r.h.al == maj && r.h.ah > min)) { maj = r.h.al; min = r.h.ah; }
        Fat32Ok = maj > 7 || (maj == 7 && min >= 10) || r.h.bh == 0xFD;   /* BH FDh: FreeDOS */
    }
    for (d = 0; d < disks && NU < UD_UNITS - UD_SPARE; ++d)
    {
        Part pt[UD_UNITS];
        const USBPAD_Disk* di = USBPAD_DiskInfo(d);
        int n = find_parts(d, pt, UD_UNITS - UD_SPARE - NU);
        for (i = 0; i < n; ++i)
        {
            Unit* x = &U[NU++];
            memset(x, 0, sizeof *x);
            x->disk = d; x->part = i; x->fat = pt[i].fat; x->start = pt[i].start; x->size = pt[i].size;
            x->gen = di ? di->gen : 0; x->ok = 1;
            if (di) x->lun = di->lun;
            x->sig = unit_sig(NU - 1);
        }
    }
    nfound = NU;
    for (i = 0; i < UD_SPARE && NU < UD_UNITS; ++i)            /* spare drives: sticks plugged in later */
    {
        Unit* x = &U[NU++];
        memset(x, 0, sizeof *x);
        x->disk = -1; x->gen = 0xFFFFFF00UL + (uint32_t)i; x->lun = -1; x->fat = 16;
    }

    memset(&r, 0, sizeof r); r.h.ah = 0x52; dos_call(&r);
    lol = DPMI_SEGOFF2L(r.w.es, r.w.bx);
    CdsBase = DPMI_SEGOFF2L(DPMI_LoadW(lol + 0x18), DPMI_LoadW(lol + 0x16));
    nblk = DPMI_LoadB(lol + 0x20); lastdrive = DPMI_LoadB(lol + 0x21);
    for (drive = nblk; drive < lastdrive && DPMI_LoadW(CdsBase + drive * UD_CDS + 0x43) & 0xC000; ++drive) {}
    if (drive + NU > lastdrive) NU = lastdrive - drive;
    if (NU <= 0) { NU = 0; sprintf(Msg, "no free drive letter (LASTDRIVE=%c)", 'A' + lastdrive - 1); return 0; }
    for (i = 0; i < NU; ++i)                                    /* letters must be free and next to each other */
        if (DPMI_LoadW(CdsBase + (drive + i) * UD_CDS + 0x43) & 0xC000) { NU = i; break; }

    Seg = (uint16_t)DPMI_HighMalloc((UD_RINFO + 8 * UD_INFOSZ + 15) / 16, TRUE);
    Cb = DPMI_AllocateRMCB_RETF(&USBDISK_Req, &Reg);
    if (!Seg || !Cb) { NU = 0; strcpy(Msg, "no memory for the drive stub"); return 0; }
    typedef char stub_fits[sizeof USBDISK_Stub <= UD_STUB ? 1 : -1]; (void)sizeof(stub_fits);
    memset(stub, 0, sizeof stub);
    memcpy(stub, USBDISK_Stub, sizeof USBDISK_Stub);
    stub[0x0A] = (uint8_t)NU;
    memcpy(stub + 0x16, &Cb, 4);
    memcpy(stub + 0x0B, "USBDSK", 6);                           /* name field (unused in block devices): USBDRV finds us */
    DPMI_LMemset(base_lin(), 0, UD_RINFO + 8 * UD_INFOSZ);
    DPMI_LMemcpy(base_lin(), DPMI_PTR2L(stub), sizeof stub);

    for (i = 0; i < NU; ++i)
    {
        uint32_t dp = dpb_lin(i);
        U[i].drive = drive + i;
        if (U[i].disk < 0 || load_bpb(i)) spare_bpb(i);         /* spare / unreadable: a placeholder until a stick comes */
        memset(&r, 0, sizeof r);
        r.h.ah = 0x53; r.w.ds = Seg; r.w.si = (uint16_t)(UD_STUB + i * UD_BPB);
        r.w.es = Seg; r.w.bp = (uint16_t)(dp - base_lin());
        if (Fat32Ok) { r.w.cx = 0x4558; r.w.dx = 0x4152; }
        dos_call(&r);
        DPMI_StoreB(dp + 0, (uint8_t)U[i].drive); DPMI_StoreB(dp + 1, (uint8_t)i);
        DPMI_StoreW(dp + 0x13, 0); DPMI_StoreW(dp + 0x15, Seg);
        DPMI_StoreB(dp + 0x18, 0xFF);                           /* not accessed yet: DOS asks BUILD BPB */
        DPMI_StoreW(dp + 0x19, 0xFFFF); DPMI_StoreW(dp + 0x1B, 0xFFFF);
    }

    DPMI_StoreB(base_lin() + 0x0A, (uint8_t)NU);

    __asm__ __volatile__("cli");
    /* DPBs: append ours (linked to each other) at the end of the chain */
    for (i = 0; i + 1 < NU; ++i) { DPMI_StoreW(dpb_lin(i) + 0x19, (uint16_t)(dpb_lin(i + 1) - base_lin())); DPMI_StoreW(dpb_lin(i) + 0x1B, Seg); }
    p = DPMI_SEGOFF2L(DPMI_LoadW(lol + 2), DPMI_LoadW(lol + 0));
    for (last = p; DPMI_LoadW(last + 0x19) != 0xFFFF; last = DPMI_SEGOFF2L(DPMI_LoadW(last + 0x1B), DPMI_LoadW(last + 0x19))) {}
    DPMI_StoreW(last + 0x19, (uint16_t)(dpb_lin(0) - base_lin())); DPMI_StoreW(last + 0x1B, Seg);
    /* device chain: right after NUL */
    nul = lol + 0x22;
    DPMI_StoreD(base_lin() + 0, DPMI_LoadD(nul));
    DPMI_StoreW(nul + 0, 0); DPMI_StoreW(nul + 2, Seg);
    /* CDS entries, from C:'s as the template (kernel-specific fields) */
    for (i = 0; i < NU; ++i)
    {
        uint32_t c = CdsBase + U[i].drive * UD_CDS;
        if (nblk > 2) DPMI_LMemcpy(c, CdsBase + 2 * UD_CDS, UD_CDS); else DPMI_LMemset(c, 0, UD_CDS);
        DPMI_LMemset(c, 0, 0x43);
        DPMI_StoreB(c + 0, (uint8_t)('A' + U[i].drive)); DPMI_StoreB(c + 1, ':'); DPMI_StoreB(c + 2, '\\');
        DPMI_StoreW(c + 0x45, (uint16_t)(dpb_lin(i) - base_lin())); DPMI_StoreW(c + 0x47, Seg);
        DPMI_StoreD(c + 0x49, 0);                               /* current directory: root */
        DPMI_StoreW(c + 0x4F, 2);                               /* backslash offset */
        DPMI_StoreW(c + 0x43, 0x4000);                          /* physical drive: valid from now */
    }
    if (drive == nblk) DPMI_StoreB(lol + 0x20, (uint8_t)(nblk + NU));
    __asm__ __volatile__("sti");

    for (i = 0; i < NU; ++i) sprintf(Letters + strlen(Letters), "%s%c:", i ? " " : "", 'A' + U[i].drive);
    for (i = 0; i < NU; ++i) info_set(i, U[i].disk >= 0 && U[i].ok ? 1 : 0);
    if (nfound)
    {
        const USBPAD_Disk* di = USBPAD_DiskInfo(U[0].disk);
        sprintf(Msg, "%d drive(s) %s, first FAT%d %lu MB (%s %s)", NU, Letters, U[0].fat,
            (unsigned long)(U[0].size / 2048), di ? di->vendor : "", di ? di->product : "");
    }
    else sprintf(Msg, "drive(s) %s ready for USB sticks plugged in now", Letters);
    return NU;
}

const char* USBDISK_Letters(void) { return Letters; }

/* usbredir.c's exFAT / NTFS drives, listed by USBDRV: [0] drive + 1, [1] 1 exFAT / 2 NTFS, [4..] stick name */
void USBDISK_RedirNote(int i, int drive, int type, const char* name)
{
    char b[UD_INFOSZ];
    if (!Seg || i < 0 || i >= 8) return;
    memset(b, 0, sizeof b);
    b[0] = (char)(drive + 1); b[1] = (char)type;
    if (name) snprintf(b + 4, sizeof b - 4, "%s", name);
    DPMI_LMemcpy(DPMI_SEGOFF2L(Seg, UD_RINFO + i * UD_INFOSZ), DPMI_PTR2L(b), sizeof b);
}
const char* USBDISK_Msg(void) { return Msg; }

void USBDISK_Uninstall(void)
{
    int i;
    if (!NU || !Seg) return;
    __asm__ __volatile__("cli");
    DPMI_StoreD(base_lin() + 0x16, 0);                          /* stub: "not ready" from now on */
    for (i = 0; i < NU; ++i) DPMI_StoreW(CdsBase + U[i].drive * UD_CDS + 0x43, 0);
    __asm__ __volatile__("sti");
    NU = 0;
}
