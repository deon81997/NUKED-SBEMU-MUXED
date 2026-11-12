/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* fsread: read-only exFAT + NTFS reader for the SBEMU USB storage redirector
 * (docs/USB-STORAGE.md 5b). Plain C99, no libc beyond memcpy/memcmp/memset.
 *
 * All disk access goes through one callback; the library never keeps a whole
 * FAT, bitmap or MFT in memory. The caller gives a work buffer at mount time
 * (FSR_WORK_MIN is always enough; fsr_mount says if a given one is too small).
 * Only one call may run on a volume at a time (no locking inside).
 *
 * Names: DOS sees 8.3 upper-case names. exFAT: generated aliases (LONGNA~1.TXT,
 * numbered in directory order). NTFS: the DOS-namespace $FILE_NAME when present,
 * else generated the same way. Path components may be 8.3 names or long names
 * (ASCII, case-insensitive through the volume up-case table for U+0000..00FF;
 * characters above U+00FF compare as-is). Generated aliases do not check for a
 * clash with a real 8.3 name that looks like an alias.
 *
 * Errors are negative DOS error codes, so the redirector can return -r.
 */
#ifndef FSREAD_H
#define FSREAD_H
#include <stdint.h>

#define FSR_OK        0
#define FSR_ENOFILE  (-2)   /* file not found */
#define FSR_ENOPATH  (-3)   /* path not found */
#define FSR_EACCES   (-5)   /* compressed / encrypted / not a file */
#define FSR_ENOMEM   (-8)   /* work buffer too small, too many $MFT runs */
#define FSR_EBADFS   (-13)  /* corrupt structure / not exFAT or NTFS */
#define FSR_ENOMORE  (-18)  /* no more files */
#define FSR_EIO      (-31)  /* read callback failed */

#define FSR_ATTR_RO   0x01
#define FSR_ATTR_HID  0x02
#define FSR_ATTR_SYS  0x04
#define FSR_ATTR_DIR  0x10
#define FSR_ATTR_ARC  0x20

#define FSR_EXFAT 1
#define FSR_NTFS  2

#define FSR_WORK_MIN  (4 * 4096 + 4096)  /* worst case: 4K records, 4K INDX, 4K sectors */
#define FSR_MFT_RUNS  128
#define FSR_DEPTH     10                 /* max NTFS index tree depth */

/* Read `count` device sectors at absolute `lba` into buf. Return 0 on success. */
typedef int (*fsr_read_fn)(void *ctx, uint64_t lba, int count, void *buf);

typedef struct { uint64_t vcn, len; int64_t lcn; } fsr_run;   /* lcn < 0: sparse */

typedef struct {               /* one directory entry as DOS sees it */
    char     name[13];         /* "LONGNA~1.TXT", NUL-terminated */
    char     lname[256];       /* long name, UTF-8, truncated to 255 bytes */
    uint8_t  attr;             /* FSR_ATTR_* */
    uint16_t time, date;       /* DOS format (NTFS: UTC) */
    uint64_t size;
} fsr_dirent;

typedef struct {               /* exFAT data stream (file or directory) */
    uint32_t first, cidx, clus;/* cidx/clus: cached position in the FAT chain */
    uint8_t  nofat;
    uint64_t size, vdl;        /* root dir: size = ~0 (follow chain) */
} fsr_xstream;

typedef struct {
    int      type;
    uint8_t  attr;
    uint16_t time, date;
    uint64_t size, pos;
    union {
        fsr_xstream x;
        struct { uint64_t mft, init; uint8_t resident; fsr_run rc; } n;
    } u;
} fsr_file;

typedef struct {               /* findfirst / findnext state */
    char     pat[11];          /* FCB pattern, '?' = any */
    uint8_t  amask;
    uint32_t ord;              /* entries returned so far (alias numbering) */
    union {
        struct { fsr_xstream d; uint32_t idx; } x;
        struct {
            uint64_t dir; int sp;                     /* sp = frames used */
            struct { uint64_t vcn; uint32_t off; uint8_t down; } st[FSR_DEPTH];
        } n;
    } u;
} fsr_find;

typedef struct {               /* alias numbering cache: entry `pos` of `dir` has number n */
    uint64_t dir; uint32_t pos, n; char key[9]; uint8_t used; fsr_find st;
} fsr_acache;

#define FSR_ACACHE 4

typedef struct fsr_vol {
    int         type;
    fsr_read_fn rd;
    void       *ctx;
    uint64_t    base;          /* partition start LBA */
    uint32_t    ss, ss_shift;  /* bytes per sector (= device sector size) */
    uint32_t    cs, cs_shift;  /* bytes per cluster */
    uint64_t    nclus;         /* clusters in the volume */
    int         dirty;         /* volume dirty flag was set (still mounted read-only) */
    uint8_t    *sec;  uint64_t sec_tag;   /* one sector, data / dirs */
    uint8_t    *sec2; uint64_t sec2_tag;  /* exFAT: FAT sector */
    uint16_t    up[256];       /* up-case table, U+0000..00FF */
    uint16_t    nm[256];       /* scratch: current UTF-16 name */
    int         nmlen;
    /* exFAT */
    uint32_t    fat_lba, heap_lba, root, bmp_clus; uint64_t bmp_len;
    /* NTFS */
    uint32_t    rec_size, idx_size, vcn_size;
    uint8_t    *reca, *recb, *idx;
    uint64_t    reca_tag, recb_tag, idx_tag, idx_dir;
    int         nmft;
    fsr_run     mft[FSR_MFT_RUNS];
    fsr_acache  ac[FSR_ACACHE]; int acn;
} fsr_vol;

/* Mount the volume whose boot sector is at absolute LBA part_lba (exFAT or NTFS,
 * told apart by the OEM ID). work must stay valid while mounted. */
int fsr_mount(fsr_vol *v, fsr_read_fn rd, void *ctx, uint64_t part_lba,
              void *work, uint32_t worksize);
/* Open a file or directory. path: "\DIR\FILE.TXT" ('/' also accepted), "" or "\"
 * = root. info (may be NULL) gets the entry (root: name "", attr DIR). */
int fsr_open(fsr_vol *v, const char *path, fsr_file *f, fsr_dirent *info);
/* Read from f->pos (set f->pos to seek). Returns bytes read (0 at EOF) or < 0.
 * Bytes past ValidDataLength / initialized size and sparse runs read as 0. */
int32_t fsr_read(fsr_vol *v, fsr_file *f, void *buf, uint32_t len);
/* fcb: 11-char FCB pattern ("????????TXT"), NULL = all. amask: DOS search
 * attributes (hidden/system/dir entries only returned if asked for).
 * "." and ".." are not returned (the redirector synthesizes them). */
int fsr_findfirst(fsr_vol *v, const char *dirpath, const char *fcb, uint8_t amask,
                  fsr_find *s, fsr_dirent *d);
int fsr_findnext(fsr_vol *v, fsr_find *s, fsr_dirent *d);
/* "*.TXT" style -> 11-char FCB pattern */
void fsr_pattern(const char *s, char fcb[11]);
/* Free clusters (scans the allocation bitmap; slow on big volumes). */
int fsr_freeclusters(fsr_vol *v, uint64_t *nfree);

/* internal, shared by exfat.c and ntfs.c */
int  fsr_sread(fsr_vol *v, uint64_t lba);                 /* lba -> v->sec (cached) */
void fsr_mkname(fsr_vol *v, fsr_dirent *d);               /* v->nm -> d->lname, 8.3 check */
int  fsr_valid83(fsr_vol *v, char *out);                  /* v->nm valid 8.3? out may be NULL */
void fsr_aliaskey(fsr_vol *v, char key[10]);              /* 6 base + 3 ext, space padded */
void fsr_alias(const char key[10], uint32_t n, char out[13]);
uint32_t fsr_tilde(const char *n83);                      /* N of NAME~N.EXT, 0 if none */
int  fsr_namecmp(fsr_vol *v, const uint16_t *a, int al, const uint16_t *b, int bl);
int  fsr_match(const char *pat, const char *name83);
int  fsr_wantattr(uint8_t amask, uint8_t attr);
int  fsr_comp(const char **p, uint16_t *c, int *cl, char n83[13]);
typedef struct { char key[4][9]; uint32_t n[4]; int k; } fsr_tscan;
int  fsr_tilde_hit(fsr_vol *v, fsr_tscan *t, const char *n83, uint32_t tn);
fsr_acache *fsr_acget(fsr_vol *v, uint64_t dir, const char *key, uint32_t pos);
void fsr_acput(fsr_vol *v, uint64_t dir, const char *key, uint32_t pos, uint32_t n, const fsr_find *st);
int  fsr_x_mount(fsr_vol *v, uint8_t *boot, uint32_t worksize);
int  fsr_n_mount(fsr_vol *v, uint8_t *boot, uint32_t worksize);
uint64_t fsr_popfree(const uint8_t *b, uint32_t n, uint64_t first, uint64_t nclus);
int  fsr_x_open(fsr_vol *v, const char *path, fsr_file *f, fsr_dirent *info);
int  fsr_n_open(fsr_vol *v, const char *path, fsr_file *f, fsr_dirent *info);
int32_t fsr_x_read(fsr_vol *v, fsr_file *f, void *buf, uint32_t len);
int32_t fsr_n_read(fsr_vol *v, fsr_file *f, void *buf, uint32_t len);
int  fsr_x_find(fsr_vol *v, const char *dir, fsr_find *s, fsr_dirent *d, int first);
int  fsr_n_find(fsr_vol *v, const char *dir, fsr_find *s, fsr_dirent *d, int first);
int  fsr_x_free(fsr_vol *v, uint64_t *nfree);
int  fsr_n_free(fsr_vol *v, uint64_t *nfree);

static inline uint16_t fsr_le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static inline uint32_t fsr_le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static inline uint64_t fsr_le64(const uint8_t *p) { return fsr_le32(p) | (uint64_t)fsr_le32(p + 4) << 32; }

#endif
