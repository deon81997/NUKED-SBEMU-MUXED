/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* fsread host test: fstest IMAGE [part_lba] [sector_size]
 * Walks the whole tree with findfirst/findnext, opens every entry by its 8.3 path
 * (and by its long path when ASCII), reads files in odd-sized chunks and prints
 *   L <longpath> <size> <fnv64>   per file,  D <longpath>  per directory
 *   A <83path> <longpath> <attr> <date> <time>  (aliases/attrs, for eyeballing)
 * Extra: fstest IMAGE lba ss cat PATH  -> file bytes to stdout. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../fsread.h"

#ifdef __DJGPP__
#define fseeko(f, o, w) fseek(f, (long)(o), w)
#define off_t long
#endif
/* device = image file; single-sector reads go through a direct-mapped cache of
 * NCACHE sectors (like the redirector's sector cache), devcmds = what a USB stick would see */
#define NCACHE 64
static FILE *img; static int ss = 512; static long reads, devcmds;
static uint8_t cdat[NCACHE][4096]; static uint64_t ctag[NCACHE];
#ifdef __DJGPP__
#include <dpmi.h>
#include <go32.h>
#include <sys/movedata.h>
static int bdrive;   /* fstest 0x81 ...: read BIOS disk 81h with INT 13h AH=42h */
static int bios(uint64_t lba, int n, uint8_t *buf)
{
    while (n > 0) {
        int k = n > 16 ? 16 : n; __dpmi_regs r; uint8_t pk[16] = { 16, 0 };
        pk[2] = (uint8_t)k; pk[4] = 32; pk[6] = (uint8_t)(__tb >> 4); pk[7] = (uint8_t)(__tb >> 12);
        memcpy(pk + 8, &lba, 8);
        dosmemput(pk, 16, __tb);
        memset(&r, 0, sizeof r);
        r.h.ah = 0x42; r.h.dl = (uint8_t)bdrive; r.x.ds = __tb >> 4; r.x.si = __tb & 15;
        __dpmi_int(0x13, &r);
        if (r.x.flags & 1) return -1;
        dosmemget(__tb + 32, k * ss, buf);
        buf += k * ss; lba += k; n -= k;
    }
    return 0;
}
#endif
static int dev(uint64_t lba, int n, void *buf)
{
    devcmds++;
#ifdef __DJGPP__
    if (bdrive) return bios(lba, n, (uint8_t *)buf);
#endif
    if (fseeko(img, (off_t)(lba * ss), SEEK_SET)) return -1;
    return fread(buf, ss, n, img) == (size_t)n ? 0 : -1;
}
static int rd(void *ctx, uint64_t lba, int n, void *buf)
{
    unsigned h = (unsigned)(lba % NCACHE); static FILE *lg; static int lgi;
    (void)ctx; reads++;
    if (!lgi) { const char *e = getenv("FSTEST_LOG"); lgi = 1; if (e) lg = fopen(e, "w"); }
    if (lg && n == 1) { int i; for (i = 0; i < n; i++) fprintf(lg, "%llu\n", (unsigned long long)(lba + i)); }
    if (n != 1) return dev(lba, n, buf);
    if (ctag[h] != lba + 1) { if (dev(lba, 1, cdat[h])) { ctag[h] = 0; return -1; } ctag[h] = lba + 1; }
    memcpy(buf, cdat[h], ss);
    return 0;
}

static fsr_vol v; static int errs;
static uint8_t buf[70000];

static int ascii(const char *s) { for (; *s; s++) if ((unsigned char)*s >= 0x80) return 0; return 1; }

static uint64_t hashfile(const char *p, uint64_t *sz, int *rc)
{
    fsr_file f; uint64_t h = 1469598103934665603ull; static const uint32_t ch[] = { 1000, 4103, 65536, 512, 1 };
    int k = 0; int32_t n;
    *rc = fsr_open(&v, p, &f, 0);
    if (*rc) return 0;
    *sz = f.size;
    while ((n = fsr_read(&v, &f, buf, ch[k++ % 5])) > 0) { int i; for (i = 0; i < n; i++) h = (h ^ buf[i]) * 1099511628211ull; }
    if (n < 0) *rc = n;
    return h;
}

static void walk(const char *p83, const char *plong)
{
    fsr_find s; fsr_dirent d; int r;
    for (r = fsr_findfirst(&v, p83, 0, 0x16, &s, &d); !r; r = fsr_findnext(&v, &s, &d)) {
        char n83[600], nl[1200]; uint64_t sz = 0, h, h2, sz2; int rc, rc2;
        fsr_file f; fsr_dirent i2;
        snprintf(n83, sizeof n83, "%s\\%s", p83, d.name);
        snprintf(nl, sizeof nl, "%s/%s", plong, d.lname);
        printf("A %s %s %02x %04x %04x\n", n83, nl, d.attr, d.date, d.time);
        rc = fsr_open(&v, n83, &f, &i2);
        if (rc == FSR_EACCES) { printf("C %s\n", nl); continue; }
        if (rc || strcmp(i2.name, d.name) || strcmp(i2.lname, d.lname) || i2.size != d.size) {
            printf("ERR open83 %s rc=%d got %s/%s\n", n83, rc, i2.name, i2.lname); errs++; continue;
        }
        if (d.attr & FSR_ATTR_DIR) { printf("D %s\n", nl); walk(n83, nl); continue; }
        h = hashfile(n83, &sz, &rc);
        if (rc) { printf("ERR read %s rc=%d\n", nl, rc); if (rc != FSR_EACCES) errs++; continue; }
        printf("L %s %llu %016llx\n", nl, (unsigned long long)sz, (unsigned long long)h);
        if (ascii(nl)) {
            char lp[1200]; int i; strcpy(lp, nl); for (i = 0; lp[i]; i++) if (lp[i] == '/') lp[i] = '\\';
            h2 = hashfile(lp, &sz2, &rc2);
            if (rc2 || h2 != h || sz2 != sz) { printf("ERR longopen %s rc=%d\n", nl, rc2); errs++; }
        }
    }
    if (r != FSR_ENOMORE) { printf("ERR find %s rc=%d\n", p83, r); errs++; }
}

int main(int argc, char **argv)
{
    static uint8_t work[FSR_WORK_MIN]; uint64_t lba = argc > 2 ? strtoull(argv[2], 0, 0) : 0, fr; int r;
    if (argc > 3) ss = atoi(argv[3]);
#ifdef __DJGPP__
    if (!strncmp(argv[1], "0x8", 3)) bdrive = (int)strtol(argv[1], 0, 0); else
#endif
    if (!(img = fopen(argv[1], "rb"))) return 2;
    if ((r = fsr_mount(&v, rd, 0, lba, work, sizeof work))) { printf("mount failed %d\n", r); return 1; }
    if (argc > 4 && !strcmp(argv[4], "bench")) {   /* timings + device commands per step */
        static const char *step[] = { "list \\MANY (1500 files)", "read \\CONTIG.BIN (3 MB)",
            "open \\MANY\\FIL~1499.DAT", "open \\many\\file with long name 1499.dat", "list \\MANY again", "read \\FRAG\\FRAGME~1.BIN (2.5 MB)" };
        int k;
        for (k = 0; k < 6; k++) {
            clock_t t = clock(); long r0 = reads, d0 = devcmds; fsr_find s; fsr_dirent d; fsr_file f; int n = 0; int32_t g;
            if (k == 0 || k == 4) { for (r = fsr_findfirst(&v, "\\MANY", 0, 0x16, &s, &d); !r; r = fsr_findnext(&v, &s, &d)) n++; }
            if (k == 1) { if (!(r = fsr_open(&v, "\\CONTIG.BIN", &f, 0))) while ((g = fsr_read(&v, &f, buf, 32768)) > 0) n += g; }
            if (k == 5) { if (!(r = fsr_open(&v, "\\FRAG\\FRAGME~1.BIN", &f, 0))) while ((g = fsr_read(&v, &f, buf, 32768)) > 0) n += g; }
            if (k == 2) r = fsr_open(&v, "\\MANY\\FIL~1499.DAT", &f, &d);
            if (k == 3) r = fsr_open(&v, "\\many\\file with long name 1499.dat", &f, &d);
            printf("%-42s %6.2f s  n=%d rc=%d  lib reads %ld  device cmds %ld\n", step[k],
                   (double)(clock() - t) / CLOCKS_PER_SEC, n, r, reads - r0, devcmds - d0);
        }
        return 0;
    }
    if (argc > 5 && !strcmp(argv[4], "cat")) {
        fsr_file f; int32_t n;
        if ((r = fsr_open(&v, argv[5], &f, 0))) { fprintf(stderr, "open %d\n", r); return 1; }
        while ((n = fsr_read(&v, &f, buf, 60000)) > 0) fwrite(buf, 1, n, stdout);
        return n < 0;
    }
    fprintf(stderr, "type %d ss %u cs %u clusters %llu dirty %d\n", v.type, v.ss, v.cs, (unsigned long long)v.nclus, v.dirty);
    walk("", "");
    r = fsr_freeclusters(&v, &fr);
    fprintf(stderr, "free rc=%d %llu, reads %ld, errors %d\n", r, (unsigned long long)fr, reads, errs);
    return errs != 0;
}
