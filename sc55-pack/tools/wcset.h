/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* wcset.h - write-combining (WC) on the VESA linear frame buffer through one free variable MTRR.
 * Shared by WCSET.EXE, MENU and MIDPLAY (include it in exactly one .c file per program: it defines a small
 * real-mode routine with global labels).
 *
 * MSRs and CR0 need ring 0, a DPMI client runs at ring 3. So the MTRR work is done by a ~100-byte real-mode routine
 * called through DPMI 0301h: with HDPMI on HIMEM only (SC55) it runs in real mode; under JEMM (SC55JEM) it runs in
 * V86 mode, where JEMM emulates RDMSR/WRMSR/WBINVD/MOV CRx (Jemm EMU.ASM). Any other V86 monitor (EMM386, QEMM)
 * is refused, as is a Windows DOS box. HDPMI maps the frame buffer with normal page attributes (no PCD/PWT), so
 * the MTRR type decides.
 * Rules (as in LordOfMice's MSR/MTRR tools): CPUID MTRR bit, MTRRcap WC bit, mask bits up to the CPU's physical
 * address width (CPUID 80000008h, else 36 with PAE, else 32); an MTRR is only written while all MTRRs are off,
 * caches disabled and flushed (Intel SDM 11.11.7.2); a range that overlaps any MTRR already set is left alone.
 * AMD K6 (family 5) has other MTRRs: skipped. Setting: WCSET.CFG "WC=AUTO|ON|OFF" next to the program, the
 * environment variable WCSET overrides it. AUTO: P6-class CPUs (Intel / AMD / VIA family 6+). ON: any CPU with
 * MTRRs, kept after exit. With AUTO, programs restore the MTRR they changed at exit (wc_restore, atexit). */
#ifndef WCSET_INCLUDED
#define WCSET_INCLUDED
#include <dpmi.h>
#include <go32.h>
#include <sys/movedata.h>
#include <sys/farptr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

typedef unsigned long long wc_u64;
enum { WC_OFF = 0, WC_AUTO = 1, WC_ON = 2 };

/* the real-mode routine. BP = 0: only SMSW; 1: RDMSR ECX -> EDX:EAX; 2: WRMSR; 3: write the MTRR pair
 * ECX (PHYSBASE) / ECX+1 (PHYSMASK) = EDX:EAX / EDI:ESI with interrupts, caches and MTRRs off. BX = MSW (bit 0 PE:
 * V86 mode) on return. */
__asm__(
    ".text\n"
    ".globl _wc_rm_beg\n_wc_rm_beg:\n"
    ".code16\n"
    "smsw %bx\n"
    "cmpw $1, %bp\n je 1f\n"
    "cmpw $2, %bp\n je 2f\n"
    "cmpw $3, %bp\n je 3f\n"
    "lret\n"
    "1: rdmsr\n lret\n"
    "2: wrmsr\n lret\n"
    "3: pushfl\n cli\n"
    "pushl %esi\n pushl %edi\n pushl %eax\n pushl %edx\n pushl %ecx\n"   /* bp+16 mlo, +12 mhi, +8 blo, +4 bhi, +0 msr */
    "movw %sp, %bp\n"
    "movl %cr0, %eax\n pushl %eax\n"                                   /* CD on, NW off */
    "orl $0x40000000, %eax\n andl $0xDFFFFFFF, %eax\n movl %eax, %cr0\n"
    "wbinvd\n"
    "movl %cr3, %eax\n movl %eax, %cr3\n"
    "movl $0x2FF, %ecx\n rdmsr\n pushl %eax\n pushl %edx\n"            /* MTRRdefType, then E off */
    "andl $0xFFFFF7FF, %eax\n wrmsr\n"
    "movl 0(%bp), %ecx\n movl 8(%bp), %eax\n movl 4(%bp), %edx\n wrmsr\n"
    "incl %ecx\n movl 16(%bp), %eax\n movl 12(%bp), %edx\n wrmsr\n"
    "wbinvd\n"
    "movl %cr3, %eax\n movl %eax, %cr3\n"
    "movl $0x2FF, %ecx\n popl %edx\n popl %eax\n wrmsr\n"
    "popl %eax\n movl %eax, %cr0\n"
    "addw $20, %sp\n popfl\n"
    "lret\n"
    ".code32\n"
    ".globl _wc_rm_end\n_wc_rm_end:\n");
extern unsigned char wc_rm_beg[], wc_rm_end[];

typedef struct { wc_u64 base, mask; } WCMTRR;
static struct
{
    int probed, ok, cpu_ok_auto;        /* ok: MTRRs can be written here; cpu_ok_auto: a P6-class CPU (AUTO) */
    char why[120];                      /* why not (when !ok) */
    char vendor[13]; int family, model, stepping;
    int phys_bits, vcnt, wc_cap, fix, def_e, def_type, v86, jemm, win;
    wc_u64 def_raw, phys_mask;
    WCMTRR m[32];
    unsigned long lfb, vram;            /* frame buffer: physical address, total video memory (bytes) */
    wc_u64 r_base, r_size;              /* the WC range for it */
    int changed;                        /* MTRR index this program set, -1 none */
    WCMTRR saved;                       /* what was in it */
    int mode;                           /* setting WC_* */
    char home[260], last[160];
} WC = { .changed = -1, .mode = WC_AUTO };

static int wc_rm(int op, unsigned long ecx, unsigned long* eax, unsigned long* edx, unsigned long esi, unsigned long edi)
{
    __dpmi_regs r; size_t n = (size_t)(wc_rm_end - wc_rm_beg);
    dosmemput(wc_rm_beg, n, __tb);
    memset(&r, 0, sizeof r);
    r.x.cs = __tb >> 4; r.x.ip = __tb & 15; r.x.flags = 0x0202;
    r.x.bp = (unsigned short)op; r.d.ecx = ecx; r.d.eax = eax ? *eax : 0; r.d.edx = edx ? *edx : 0; r.d.esi = esi; r.d.edi = edi;
    if (__dpmi_simulate_real_mode_procedure_retf(&r) != 0) return -1;
    if (eax) *eax = r.d.eax;
    if (edx) *edx = r.d.edx;
    return r.x.bx;
}
static wc_u64 wc_rdmsr(unsigned long msr) { unsigned long a = 0, d = 0; wc_rm(1, msr, &a, &d, 0, 0); return ((wc_u64)d << 32) | a; }
static void wc_write_pair(int i, wc_u64 base, wc_u64 mask)
{
    unsigned long a = (unsigned long)base, d = (unsigned long)(base >> 32);
    wc_rm(3, 0x200 + 2 * i, &a, &d, (unsigned long)mask, (unsigned long)(mask >> 32));
}
static void wc_cpuid(unsigned long leaf, unsigned long* a, unsigned long* b, unsigned long* c, unsigned long* d)
{
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0));
}
static int wc_has_cpuid(void)
{
    unsigned long f1, f2;
    __asm__ volatile("pushfl; popl %0; movl %0, %1; xorl $0x200000, %0; pushl %0; popfl; pushfl; popl %0; pushl %1; popfl"
                     : "=&r"(f1), "=&r"(f2));
    return ((f1 ^ f2) & 0x200000) != 0;
}
static const char* wc_type_name(int t)
{
    switch (t) { case 0: return "UC"; case 1: return "WC"; case 4: return "WT"; case 5: return "WP"; case 6: return "WB"; default: return "??"; }
}
static int wc_valid(int i) { return (WC.m[i].mask & 0x800) != 0; }
static wc_u64 wc_mbase(int i) { return WC.m[i].base & WC.phys_mask & ~0xFFFULL; }
static wc_u64 wc_mmask(int i) { return WC.m[i].mask & WC.phys_mask & ~0xFFFULL; }
static wc_u64 wc_msize(int i) { return ((~wc_mmask(i)) & WC.phys_mask) + 1; }   /* for a contiguous mask */

static int wc_is_jemm(void)
{
    static const char* nm[2] = { "EMMXXXX0", "EMMQXXX0" };
    int k;
    for (k = 0; k < 2; k++)
    {
        __dpmi_regs r; unsigned short h; unsigned char b[6] = { 0 };
        dosmemput(nm[k], 9, __tb);
        memset(&r, 0, sizeof r); r.x.ax = 0x3D00; r.x.ds = __tb >> 4; r.x.dx = __tb & 15; __dpmi_int(0x21, &r);
        if (r.x.flags & 1) continue;
        h = r.x.ax;
        dosmemput(b, 6, __tb + 16);
        memset(&r, 0, sizeof r); r.x.ax = 0x4402; r.x.bx = h; r.x.cx = 6; r.x.ds = (__tb + 16) >> 4; r.x.dx = (__tb + 16) & 15; __dpmi_int(0x21, &r);
        { int ok = !(r.x.flags & 1); dosmemget(__tb + 16, 6, b);
          memset(&r, 0, sizeof r); r.x.ax = 0x3E00; r.x.bx = h; __dpmi_int(0x21, &r);
          if (ok && b[0] == 0x28 && b[1] == 0) return 1; }
    }
    return 0;
}
/* 1: a Windows (or OS/2, NT) DOS box */
static int wc_in_windows(void)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r); r.x.ax = 0x1600; __dpmi_int(0x2F, &r);
    if (r.h.al != 0 && r.h.al != 0x80) return 1;
    memset(&r, 0, sizeof r); r.x.ax = 0x3306; __dpmi_int(0x21, &r);
    if (r.x.bx == 0x3205) return 1;                                     /* NT's DOS 5.50 */
    memset(&r, 0, sizeof r); r.x.ax = 0x3000; __dpmi_int(0x21, &r);
    if (r.h.al >= 10) return 1;                                         /* OS/2 */
    return 0;
}

/* the VESA linear frame buffer: address of the first LFB mode, total video memory */
static void wc_find_lfb(void)
{
    __dpmi_regs r; unsigned char vi[512], mi[256]; unsigned long lp; int n;
    if (WC.lfb && WC.vram) return;
    memset(&r, 0, sizeof r); dosmemput("VBE2", 4, __tb);
    r.x.ax = 0x4F00; r.x.es = __tb >> 4; r.x.di = __tb & 15; __dpmi_int(0x10, &r);
    if (r.x.ax != 0x004F) return;
    dosmemget(__tb, 512, vi);
    if (memcmp(vi, "VESA", 4) || vi[5] < 2) return;
    WC.vram = (unsigned long)(vi[0x12] | (vi[0x13] << 8)) << 16;
    if (WC.lfb) return;
    lp = ((unsigned long)(vi[0x10] | (vi[0x11] << 8)) << 4) + (vi[0x0E] | (vi[0x0F] << 8));
    for (n = 0; n < 200; n++)
    {
        unsigned short m; unsigned long phys;
        dosmemget(lp + n * 2, 2, &m);
        if (m == 0xFFFF) break;
        memset(&r, 0, sizeof r); r.x.ax = 0x4F01; r.x.cx = m; r.x.es = __tb >> 4; r.x.di = __tb & 15; __dpmi_int(0x10, &r);
        if (r.x.ax != 0x004F) continue;
        dosmemget(__tb, 256, mi);
        if (!(mi[0] & 1) || !(mi[0] & 0x80)) continue;
        phys = mi[0x28] | (mi[0x29] << 8) | ((unsigned long)mi[0x2A] << 16) | ((unsigned long)mi[0x2B] << 24);
        if (phys) { WC.lfb = phys; break; }
    }
}
/* the WC range: the whole video memory, a power of two aligned to its size (smaller if the LFB is not aligned) */
static int wc_range(void)
{
    wc_u64 sz = 4096, need = WC.vram ? WC.vram : 0x400000;
    if (!WC.lfb) return 0;
    while (sz < need) sz <<= 1;
    while (sz > 4096 && (WC.lfb & (sz - 1))) sz >>= 1;
    if (sz < 0x100000) return 0;                                         /* under 1 MB: not even 640x480 */
    WC.r_base = WC.lfb; WC.r_size = sz;
    return 1;
}

static void wc_why(const char* s) { snprintf(WC.why, sizeof WC.why, "%s", s); }
static void wc_read_mtrrs(void)
{
    int i;
    for (i = 0; i < WC.vcnt && i < 32; i++) { WC.m[i].base = wc_rdmsr(0x200 + 2 * i); WC.m[i].mask = wc_rdmsr(0x201 + 2 * i); }
    WC.def_raw = wc_rdmsr(0x2FF); WC.def_e = (WC.def_raw >> 11) & 1; WC.fix = (WC.def_raw >> 10) & 1; WC.def_type = (int)(WC.def_raw & 0xFF);
}
/* everything about this PC; WC.ok = MTRRs can be written. Safe anywhere: no MSR is touched unless CPUID says so */
static int wc_probe(void)
{
    unsigned long a, b, c, d;
    if (WC.probed) return WC.ok;
    WC.probed = 1; WC.ok = 0;
    if ((WC.win = wc_in_windows())) { wc_why("Windows DOS box: Windows owns the MTRRs (its display driver sets write-combining)"); return 0; }
    if (!wc_has_cpuid()) { wc_why("no CPUID (386/486): no MTRRs"); return 0; }
    wc_cpuid(0, &a, &b, &c, &d);
    memcpy(WC.vendor, &b, 4); memcpy(WC.vendor + 4, &d, 4); memcpy(WC.vendor + 8, &c, 4); WC.vendor[12] = 0;
    if (a < 1) { wc_why("CPUID without leaf 1"); return 0; }
    wc_cpuid(1, &a, &b, &c, &d);
    WC.family = (a >> 8) & 15; WC.model = (a >> 4) & 15; WC.stepping = a & 15;
    if (WC.family == 15) { WC.family += (a >> 20) & 0xFF; WC.model |= ((a >> 16) & 15) << 4; }
    else if (WC.family == 6) WC.model |= ((a >> 16) & 15) << 4;
    if (!strcmp(WC.vendor, "AuthenticAMD") && WC.family == 5) { wc_why("AMD K5/K6: not P6-style MTRRs (skipped)"); return 0; }
    if (!(d & (1 << 5)) || !(d & (1 << 12))) { wc_why("this CPU has no MTRRs (needs Pentium Pro/II/III/M, Athlon or newer)"); return 0; }
    WC.cpu_ok_auto = WC.family >= 6 && (!strcmp(WC.vendor, "GenuineIntel") || !strcmp(WC.vendor, "AuthenticAMD") ||
                                         !strcmp(WC.vendor, "CentaurHauls"));
    WC.phys_bits = (d & (1 << 6)) ? 36 : 32;
    wc_cpuid(0x80000000UL, &a, &b, &c, &d);
    if (a >= 0x80000008UL && a < 0x8000FFFFUL) { wc_cpuid(0x80000008UL, &a, &b, &c, &d); if ((a & 0xFF) >= 32 && (a & 0xFF) <= 52) WC.phys_bits = a & 0xFF; }
    WC.phys_mask = (1ULL << WC.phys_bits) - 1;
    { int msw = wc_rm(0, 0, NULL, NULL, 0, 0);
      if (msw < 0) { wc_why("the DPMI host refused the real-mode call"); return 0; }
      WC.v86 = msw & 1; }
    if (WC.v86 && !(WC.jemm = wc_is_jemm())) { wc_why("V86 mode under EMM386/QEMM: only JEMM (or no EMM) is supported"); return 0; }
    { wc_u64 cap = wc_rdmsr(0xFE); WC.vcnt = (int)(cap & 0xFF); WC.wc_cap = (int)((cap >> 10) & 1); }
    if (WC.vcnt > 32) WC.vcnt = 32;
    wc_read_mtrrs();
    if (!WC.wc_cap) { wc_why("MTRRcap: this CPU has no write-combining type"); return 0; }
    if (!WC.vcnt) { wc_why("MTRRcap: no variable MTRRs"); return 0; }
    if (!WC.def_e) { wc_why("MTRRs are switched off (BIOS left MTRRdefType.E = 0)"); return 0; }
    WC.ok = 1;
    return 1;
}

/* where our range stands: 1 already WC (MTRR *idx), 0 free (slot *idx, -1 none), -1 overlaps MTRR *idx (left alone) */
static int wc_check(int* idx)
{
    int i; wc_u64 ob = WC.r_base, om = (~(WC.r_size - 1)) & WC.phys_mask;
    *idx = -1;
    for (i = 0; i < WC.vcnt; i++)
    {
        wc_u64 b, m;
        if (!wc_valid(i)) continue;
        b = wc_mbase(i); m = wc_mmask(i);
        if ((ob ^ b) & m & om) continue;                                 /* no common address */
        if ((WC.m[i].base & 0xFF) == 1 && !(m & ~om) && !((ob ^ b) & m)) { *idx = i; return 1; }   /* WC, covers ours */
        *idx = i; return -1;
    }
    for (i = 0; i < WC.vcnt; i++) if (!wc_valid(i)) { *idx = i; break; }
    return 0;
}

static void wc_log(const char* who, const char* msg)
{   /* one line in SBDETAIL.TXT (next to SBEMULOG, or SBEMUDETAIL, or the program's folder), not twice the same */
    char p[300], line[200], *buf; const char *dn = getenv("SBEMUDETAIL"), *ln = getenv("SBEMULOG"); FILE* f; long sz;
    snprintf(WC.last, sizeof WC.last, "%s", msg);
    if (dn && *dn) { if (!stricmp(dn, "OFF")) return; snprintf(p, sizeof p, "%s", dn); }
    else if (ln && *ln) { char* e; snprintf(p, sizeof p - 16, "%s", ln); e = strrchr(p, '\\'); if (!e) e = strrchr(p, ':'); strcpy(e ? e + 1 : p, "SBDETAIL.TXT"); }
    else snprintf(p, sizeof p, "%s\\SBDETAIL.TXT", WC.home[0] ? WC.home : ".");
    if (access(p, F_OK) != 0) return;                                   /* only next to an SBEMU start-up log */
    snprintf(line, sizeof line, "[wc] Video write-combining (%s): %s", who, msg);
    if ((f = fopen(p, "rb")) == NULL) return;
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    if (sz > 0 && sz < 512 * 1024 && (buf = malloc(sz + 1)) != NULL)
    {
        int dup; size_t got = fread(buf, 1, sz, f); buf[got] = 0; dup = strstr(buf, line) != NULL; free(buf);
        if (dup) { fclose(f); return; }
    }
    fclose(f);
    if ((f = fopen(p, "a")) != NULL) { fprintf(f, "%s\r\n", line); fclose(f); }
}
static void wc_restore(void)
{
    if (WC.changed < 0) return;
    wc_write_pair(WC.changed, WC.saved.base, WC.saved.mask);
    WC.m[WC.changed] = WC.saved;
    WC.changed = -1;
}
/* set WC over the range; keep: 0 = this program restores it at exit. Returns 1 on (already) WC, 0 not, msg = why */
static int wc_enable(int keep, char* msg, size_t n)
{
    int i, st;
    if (!wc_probe()) { snprintf(msg, n, "off - %s", WC.why); return 0; }
    wc_find_lfb();
    if (!wc_range()) { snprintf(msg, n, "off - no VESA 2.0 linear frame buffer found"); return 0; }
    st = wc_check(&i);
    if (st == 1) { snprintf(msg, n, "already on (MTRR %d, set before; left as it is) at %08lX", i, WC.lfb); return 1; }
    if (st < 0)
    {
        snprintf(msg, n, "off - MTRR %d (%s %08llX, %llu MB) covers part of the frame buffer %08lX; not changed",
                 i, wc_type_name((int)(WC.m[i].base & 0xFF)), wc_mbase(i), wc_msize(i) >> 20, WC.lfb);
        return 0;
    }
    if (i < 0) { snprintf(msg, n, "off - all %d variable MTRRs are in use (none changed)", WC.vcnt); return 0; }
    {
        wc_u64 base = WC.r_base | 1, mask = ((~(WC.r_size - 1)) & WC.phys_mask) | 0x800;
        WCMTRR old = WC.m[i];
        wc_write_pair(i, base, mask);
        WC.m[i].base = wc_rdmsr(0x200 + 2 * i); WC.m[i].mask = wc_rdmsr(0x201 + 2 * i);
        if ((WC.m[i].base & ~0xF00ULL) != (base & ~0xF00ULL) || !(WC.m[i].mask & 0x800))
        {   /* did not stick: put the old value back */
            wc_write_pair(i, old.base, old.mask); WC.m[i] = old;
            snprintf(msg, n, "off - MTRR %d did not take the new value", i); return 0;
        }
        if (!keep) { WC.changed = i; WC.saved = old; }
        snprintf(msg, n, "ON (MTRR %d = WC %08llX, %llu MB, %d-bit addresses%s%s)", i, WC.r_base, WC.r_size >> 20,
                 WC.phys_bits, WC.v86 ? ", JEMM" : "", keep ? ", kept" : ", undone at exit");
    }
    return 1;
}
/* remove a WC MTRR that is exactly our range (WCSET ON's); nothing else is touched */
static int wc_disable(char* msg, size_t n)
{
    int i, st;
    if (!wc_probe()) { snprintf(msg, n, "%s", WC.why); return 0; }
    wc_find_lfb();
    if (!wc_range()) { snprintf(msg, n, "no VESA 2.0 linear frame buffer found"); return 0; }
    st = wc_check(&i);
    if (st != 1) { snprintf(msg, n, "no write-combining MTRR on the frame buffer: nothing to do"); return 1; }
    if (wc_mbase(i) != WC.r_base || wc_msize(i) != WC.r_size)
    { snprintf(msg, n, "MTRR %d (WC %08llX, %llu MB) was not set by WCSET: left alone", i, wc_mbase(i), wc_msize(i) >> 20); return 0; }
    if (WC.changed == i) wc_restore();
    else { wc_write_pair(i, 0, 0); WC.m[i].base = wc_rdmsr(0x200 + 2 * i); WC.m[i].mask = wc_rdmsr(0x201 + 2 * i); }
    snprintf(msg, n, "off (MTRR %d cleared)", i);
    return 1;
}

static int wc_parse_mode(const char* s)
{
    while (*s == ' ') s++;
    if (!strnicmp(s, "OFF", 3) || *s == '0') return WC_OFF;
    if (!strnicmp(s, "ON", 2) || *s == '1') return WC_ON;
    return WC_AUTO;
}
static const char* wc_mode_name(int m) { return m == WC_OFF ? "OFF" : m == WC_ON ? "ON" : "AUTO"; }
static void wc_load_setting(void)
{
    char p[300], l[80]; FILE* f; const char* e = getenv("WCSET");
    WC.mode = WC_AUTO;
    snprintf(p, sizeof p, "%s\\WCSET.CFG", WC.home[0] ? WC.home : ".");
    if ((f = fopen(p, "r")) != NULL)
    {
        while (fgets(l, sizeof l, f)) if (!strnicmp(l, "WC=", 3)) WC.mode = wc_parse_mode(l + 3);
        fclose(f);
    }
    if (e && *e) WC.mode = wc_parse_mode(e);
}
static int wc_save_setting(int mode)
{
    char p[300]; FILE* f;
    snprintf(p, sizeof p, "%s\\WCSET.CFG", WC.home[0] ? WC.home : ".");
    if ((f = fopen(p, "w")) == NULL) return 0;
    fprintf(f, "WC=%s\n", wc_mode_name(mode)); fclose(f); WC.mode = mode;
    return 1;
}
static void wc_set_home(const char* home) { if (home && *home) snprintf(WC.home, sizeof WC.home, "%s", home); }

/* MENU / MIDPLAY / the 3D screens: by the setting, WC on until the program ends. who = program name for the log.
 * lfb: a frame buffer address the caller found (0 = look it up). Runs the work once per program and address. */
static __attribute__((unused)) void wc_startup(const char* who, unsigned long lfb)
{
    static int done; static unsigned long done_lfb;
    char msg[160];
    if (done && (!lfb || lfb == done_lfb)) return;
    if (!done) { wc_load_setting(); atexit(wc_restore); }
    done = 1;
    if (lfb) { if (WC.changed >= 0 && lfb != WC.lfb) wc_restore(); WC.lfb = lfb; }
    if (WC.mode == WC_OFF) { done_lfb = lfb; wc_log(who, "off (setting WC=OFF)"); return; }
    if (!wc_probe()) { done_lfb = lfb; snprintf(msg, sizeof msg, "off - %s", WC.why); wc_log(who, msg); return; }
    if (WC.mode == WC_AUTO && !WC.cpu_ok_auto)
    { done_lfb = lfb; snprintf(msg, sizeof msg, "off - AUTO: %s family %d is not on the P6 list (WCSET ON forces it)", WC.vendor, WC.family); wc_log(who, msg); return; }
    wc_enable(WC.mode == WC_ON, msg, sizeof msg);                       /* ON: stays on (games too); AUTO: undone at exit */
    done_lfb = WC.lfb;
    wc_log(who, msg);
}
#endif
