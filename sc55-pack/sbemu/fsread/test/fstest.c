/* fsread host test: fstest IMAGE [part_lba] [sector_size]
 * Walks the whole tree with findfirst/findnext, opens every entry by its 8.3 path
 * (and by its long path when ASCII), reads files in odd-sized chunks and prints
 *   L <longpath> <size> <fnv64>   per file,  D <longpath>  per directory
 *   A <83path> <longpath> <attr> <date> <time>  (aliases/attrs, for eyeballing)
 * Extra: fstest IMAGE lba ss cat PATH  -> file bytes to stdout. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../fsread.h"

static FILE *img; static int ss = 512; static long reads;
static int rd(void *ctx, uint64_t lba, int n, void *buf)
{
    (void)ctx; reads++;
    if (fseeko(img, (off_t)(lba * ss), SEEK_SET)) return -1;
    return fread(buf, ss, n, img) == (size_t)n ? 0 : -1;
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
    if (!(img = fopen(argv[1], "rb"))) return 2;
    if ((r = fsr_mount(&v, rd, 0, lba, work, sizeof work))) { printf("mount failed %d\n", r); return 1; }
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
