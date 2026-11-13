/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU */
/* dosstub.h - just enough of DJGPP for a 32-bit Linux build of MIDPLAY's 3D view under ASan/UBSan (tests/m3host).
 * A fake 1 MB DOS memory, a fake VESA BIOS (INT 10h 4F00/01/02/05/09/0B, mode list set by the test) and a fake
 * linear frame buffer that aborts on any write outside the visible screen. */
#ifndef DOSSTUB_H
#define DOSSTUB_H
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
typedef union {
    struct { unsigned long edi, esi, ebp, res, ebx, edx, ecx, eax; } d;
    struct { unsigned short di, di_hi, si, si_hi, bp, bp_hi, res, res_hi, bx, bx_hi, dx, dx_hi, cx, cx_hi, ax, ax_hi,
             flags, es, ds, fs, gs, ip, cs, sp, ss; } x;
    struct { unsigned char edi[4], esi[4], ebp[4], res[4], bl, bh, ebx_b2, ebx_b3, dl, dh, edx_b2, edx_b3,
             cl, ch, ecx_b2, ecx_b3, al, ah, eax_b2, eax_b3; } h;
} __dpmi_regs;
typedef struct { unsigned long handle, size, address; } __dpmi_meminfo;
typedef struct { int w, h, bpp, mode; } HS_MODE;
extern unsigned char hs_dos[0x110000];
extern HS_MODE hs_modes[64]; extern int hs_nmodes, hs_fail_set;
extern unsigned char* hs_lfb; extern unsigned long hs_lfb_size; extern int hs_cur_mode;
#define __tb 0x90000UL
#define _dos_ds 1
static inline int _my_ds(void) { return 2; }
void hs_check(unsigned long a, unsigned long n, const char* what);
unsigned long hs_ticks(void);
static inline unsigned char _farpeekb(int s, unsigned long o) { (void)s; hs_check(o, 1, "peekb"); return hs_dos[o]; }
static inline unsigned short _farpeekw(int s, unsigned long o) { unsigned short v; (void)s; hs_check(o, 2, "peekw"); memcpy(&v, hs_dos + o, 2); return v; }
static inline unsigned long _farpeekl(int s, unsigned long o) { unsigned v; (void)s; if (o == 0x46C) return hs_ticks(); hs_check(o, 4, "peekl"); memcpy(&v, hs_dos + o, 4); return v; }
static inline void _farpokeb(int s, unsigned long o, unsigned char v) { (void)s; hs_check(o, 1, "pokeb"); hs_dos[o] = v; }
static inline void _farpokew(int s, unsigned long o, unsigned short v) { (void)s; hs_check(o, 2, "pokew"); memcpy(hs_dos + o, &v, 2); }
static inline void _farpokel(int s, unsigned long o, unsigned long v) { unsigned x = (unsigned)v; (void)s; hs_check(o, 4, "pokel"); memcpy(hs_dos + o, &x, 4); }
static inline void dosmemput(const void* src, int n, unsigned long a) { hs_check(a, (unsigned long)n, "dosmemput"); memcpy(hs_dos + a, src, (size_t)n); }
static inline void dosmemget(unsigned long a, int n, void* dst) { hs_check(a, (unsigned long)n, "dosmemget"); memcpy(dst, hs_dos + a, (size_t)n); }
void movedata(unsigned ss, unsigned so, unsigned ds, unsigned dofs, size_t n);
int __dpmi_int(int v, __dpmi_regs* r);
static inline int __dpmi_physical_address_mapping(__dpmi_meminfo* m) { (void)m; return 0; }
static inline int __dpmi_free_physical_address_mapping(__dpmi_meminfo* m) { (void)m; return 0; }
static inline int __dpmi_allocate_ldt_descriptors(int n) { (void)n; return 3; }
static inline int __dpmi_free_ldt_descriptor(int s) { (void)s; return 0; }
static inline int __dpmi_set_segment_base_address(int s, unsigned long a) { (void)s; (void)a; return 0; }
static inline int __dpmi_set_segment_limit(int s, unsigned long l) { (void)s; (void)l; return 0; }
static inline void __dpmi_yield(void) {}
#ifdef HS_HOOKS   /* tests/m3host/menuasan.c: scripted keys, MPU-401, a fake disk */
unsigned char inportb(unsigned short p); void outportb(unsigned short p, unsigned char v);
int kbhit(void); int getch(void);
struct ffblk; int findfirst(const char* p, struct ffblk* f, int a); int findnext(struct ffblk* f);
#else
static inline unsigned char inportb(unsigned short p) { static unsigned char t; if (p == 0x3DA) return (unsigned char)((t ^= 8) | 0); return 0xFF; }
static inline void outportb(unsigned short p, unsigned char v) { (void)p; (void)v; }
static inline int kbhit(void) { return 0; }
static inline int getch(void) { return 27; }
#endif
static inline unsigned short inportw(unsigned short p) { (void)p; return 0xFFFF; }
static inline void outportw(unsigned short p, unsigned short v) { (void)p; (void)v; }
/* conio / dos / dir */
#define _NOCURSOR 0
#define _SOLIDCURSOR 1
#define _NORMALCURSOR 2
static inline void _setcursortype(int t) { (void)t; }
#define stricmp strcasecmp
#define strnicmp strncasecmp
typedef long long uclock_t;
#define UCLOCKS_PER_SEC 1193180
uclock_t uclock(void);
static inline void _fixpath(const char* i, char* o) { strcpy(o, i); }
static inline void textattr(int a) { (void)a; }
static inline void clrscr(void) {}
static inline void setdisk(int d) { (void)d; }
static inline int getdisk(void) { return 2; }
static inline void delay(unsigned ms) { (void)ms; }
#define FA_RDONLY 1
#define FA_HIDDEN 2
#define FA_SYSTEM 4
#define FA_LABEL 8
#define FA_DIREC 16
#define FA_ARCH 32
struct ffblk { char lfn_magic[6]; short lfn_handle; unsigned short lfn_ctime, lfn_cdate, lfn_atime, lfn_adate;
               char ff_reserved[5]; unsigned char ff_attrib; unsigned short ff_ftime, ff_fdate; unsigned long ff_fsize; char ff_name[260]; };
#ifndef HS_HOOKS
static inline int findfirst(const char* p, struct ffblk* f, int a) { (void)p; (void)f; (void)a; return -1; }
static inline int findnext(struct ffblk* f) { (void)f; return -1; }
#endif
#ifndef D_OK
#define D_OK 0x10
#endif
#endif
