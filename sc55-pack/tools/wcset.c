/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* WCSET - write-combining on the VESA frame buffer (faster DOS graphics on Pentium Pro/II/III/M, Athlon and newer).
 *   WCSET [LIST]        CPU, MTRRs, frame buffer, setting, what MENU/MIDPLAY will do
 *   WCSET ON            WC on now (stays until reboot); setting ON
 *   WCSET OFF           removes WCSET's WC MTRR now; setting OFF (MENU/MIDPLAY leave the MTRRs alone)
 *   WCSET AUTO          setting AUTO (default): MENU/MIDPLAY switch WC on while they run on P6-class CPUs
 *   WCSET /BENCH        frame buffer write speed (VIDSPEED-style) with WC off and on; WCBENCH.TXT
 *   WCSET /ASK          LIST, then asks for the setting (the menu's Settings item)
 * Exit code: 0 done / WC on, 1 not possible here, 2 usage. Refuses in a Windows DOS box. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <pc.h>
#include <sys/nearptr.h>
#include <sys/stat.h>
#include <crt0.h>
void _fixpath(const char*, char*);
#include "wcset.h"
#ifndef PACKVER_STR
#define PACKVER_STR "dev"
#endif

static void list(void)
{
    int i, st, idx;
    printf("WCSET %s - write-combining on the VESA frame buffer\n", PACKVER_STR);
    wc_probe();
    if (WC.vendor[0]) printf("CPU: %s family %d model %d stepping %d%s\n", WC.vendor, WC.family, WC.model, WC.stepping,
                             WC.cpu_ok_auto ? " (P6 class)" : "");
    wc_find_lfb();
    if (WC.lfb) printf("Frame buffer: %08lX, %lu MB video memory\n", WC.lfb, WC.vram >> 20);
    else printf("Frame buffer: none (no VESA 2.0 linear frame buffer)\n");
    printf("Setting: %s (WCSET.CFG%s)\n", wc_mode_name(WC.mode), getenv("WCSET") ? ", WCSET= in the environment wins" : "");
    if (!WC.ok) { printf("MTRRs: not usable here - %s\n", WC.why); return; }
    printf("Mode: %s; %d-bit physical addresses; MTRRs %s, default type %s, fixed %s; %d variable:\n",
           WC.v86 ? "V86 (JEMM)" : "real mode", WC.phys_bits, WC.def_e ? "on" : "off", wc_type_name(WC.def_type), WC.fix ? "on" : "off", WC.vcnt);
    for (i = 0; i < WC.vcnt; i++)
    {
        if (!wc_valid(i)) { printf("  %d: free\n", i); continue; }
        printf("  %d: %s %010llX - %010llX (%llu %s)\n", i, wc_type_name((int)(WC.m[i].base & 0xFF)), wc_mbase(i),
               wc_mbase(i) + wc_msize(i) - 1, wc_msize(i) >= 0x100000 ? wc_msize(i) >> 20 : wc_msize(i) >> 10,
               wc_msize(i) >= 0x100000 ? "MB" : "KB");
    }
    if (wc_range())
    {
        st = wc_check(&idx);
        printf("Frame buffer range %08llX, %llu MB: %s\n", WC.r_base, WC.r_size >> 20,
               st == 1 ? "write-combined" : st < 0 ? "overlaps an MTRR set by the BIOS: WCSET leaves it alone" :
               idx < 0 ? "no free MTRR" : "uncached / as the BIOS set it (WC possible)");
        if (st == 0 && idx >= 0)
            printf("MENU/MIDPLAY: %s\n", WC.mode == WC_OFF ? "leave it (OFF)" : WC.mode == WC_AUTO && !WC.cpu_ok_auto ?
                   "leave it (AUTO: CPU not on the P6 list)" : "WC on while they run, MTRR restored at exit");
    }
}

/* ---- benchmark: bytes written to the frame buffer in 4 BIOS ticks (220 ms), as VIDSPEED */
static int g_sel; static unsigned long g_fb; static unsigned char* g_src;
static unsigned long ticks(void) { return _farpeekl(_dos_ds, 0x46C); }
static double run(int kind)
{
    unsigned long t0, bytes = 0, off = 0, chunk = 65536;
    if (chunk > g_fb) chunk = g_fb & ~3UL;
    t0 = ticks(); while (ticks() == t0) ;
    t0 = ticks();
    while (ticks() - t0 < 4)
    {
        if (off + chunk > g_fb) off = 0;
        if (kind == 0)
            __asm__ volatile("pushl %%es; movw %w0, %%es; cld; rep stosl; popl %%es"
                             : : "d"(g_sel), "D"(off), "c"(chunk / 4), "a"(0x5A5A5A5AUL) : "memory");
        else if (kind == 1)
            __asm__ volatile("pushl %%es; movw %w0, %%es; cld; rep stosb; popl %%es"
                             : : "d"(g_sel), "D"(off), "c"(chunk), "a"(0x33) : "memory");
        else
            __asm__ volatile("pushl %%es; movw %w0, %%es; cld; rep movsl; popl %%es"
                             : : "d"(g_sel), "D"(off), "S"(g_src), "c"(chunk / 4) : "memory");
        off += chunk; bytes += chunk;
    }
    return bytes / (4 * 0.0549254) / 1048576.0;
}
static int bench(void)
{
    __dpmi_regs r; unsigned char vi[512], mi[256]; unsigned long lp, phys = 0; int n, best = -1, bw = 0, bb = 0, pitch = 0, h = 0;
    __dpmi_meminfo map; double res[2][3]; int have[2] = { 0, 0 }, st, idx, k, state0;
    char msg[160], line[260]; FILE* f; const char* name[3] = { "dword write (rep stosd)", "byte write (rep stosb)", "copy from RAM (rep movsd)" };
    memset(&r, 0, sizeof r); dosmemput("VBE2", 4, __tb);
    r.x.ax = 0x4F00; r.x.es = __tb >> 4; r.x.di = __tb & 15; __dpmi_int(0x10, &r);
    if (r.x.ax != 0x004F) { puts("No VESA BIOS."); return 1; }
    dosmemget(__tb, 512, vi);
    lp = ((unsigned long)(vi[0x10] | (vi[0x11] << 8)) << 4) + (vi[0x0E] | (vi[0x0F] << 8));
    for (n = 0; n < 200; n++)
    {   /* 640x480 at the most bits per pixel, with a linear frame buffer */
        unsigned short m; int w, hh, b;
        dosmemget(lp + n * 2, 2, &m); if (m == 0xFFFF) break;
        memset(&r, 0, sizeof r); r.x.ax = 0x4F01; r.x.cx = m; r.x.es = __tb >> 4; r.x.di = __tb & 15; __dpmi_int(0x10, &r);
        if (r.x.ax != 0x004F) continue;
        dosmemget(__tb, 256, mi);
        if (!(mi[0] & 1) || !(mi[0] & 0x80) || !(mi[0] & 0x10)) continue;
        w = mi[0x12] | (mi[0x13] << 8); hh = mi[0x14] | (mi[0x15] << 8); b = mi[0x19];
        if (w != 640 || hh != 480 || b < 8 || b <= bb) continue;
        best = m; bb = b; bw = w; h = hh;
        pitch = (vi[5] >= 3 && (mi[0x32] | (mi[0x33] << 8))) ? mi[0x32] | (mi[0x33] << 8) : mi[0x10] | (mi[0x11] << 8);
        phys = mi[0x28] | (mi[0x29] << 8) | ((unsigned long)mi[0x2A] << 16) | ((unsigned long)mi[0x2B] << 24);
    }
    if (best < 0 || !phys) { puts("No 640x480 VESA mode with a linear frame buffer."); return 1; }
    WC.lfb = phys; wc_find_lfb();
    g_fb = (unsigned long)pitch * h & ~65535UL; if (!g_fb) g_fb = 65536;
    map.address = phys; map.size = (unsigned long)pitch * h;
    if (__dpmi_physical_address_mapping(&map) != 0) { puts("DPMI could not map the frame buffer."); return 1; }
    g_sel = __dpmi_allocate_ldt_descriptors(1);
    __dpmi_set_segment_base_address(g_sel, map.address);
    __dpmi_set_segment_limit(g_sel, ((unsigned long)pitch * h - 1) | 0xFFF);
    g_src = malloc(65536); if (!g_src) return 1;
    for (n = 0; n < 65536; n++) g_src[n] = (unsigned char)(n * 7);
    wc_probe();
    st = wc_range() && WC.ok ? wc_check(&idx) : -2;
    state0 = st == 1;
    memset(&r, 0, sizeof r); r.x.ax = 0x4F02; r.x.bx = (unsigned short)(best | 0x4000); __dpmi_int(0x10, &r);
    if (r.x.ax != 0x004F) { puts("Mode set failed."); return 1; }
    /* as it is now, then the other state (if WCSET may change it), then back */
    for (k = 0; k < 3; k++) res[state0][k] = run(k);
    have[state0] = 1;
    if (st == 0 && idx >= 0)
    {   /* off now: on for the test, undone after */
        if (wc_enable(0, msg, sizeof msg)) { for (k = 0; k < 3; k++) res[1][k] = run(k); have[1] = 1; wc_restore(); }
    }
    else if (st == 1 && wc_mbase(idx) == WC.r_base && wc_msize(idx) == WC.r_size)
    {   /* WCSET's own WC: off for the test, then on again */
        WCMTRR keep = WC.m[idx];
        wc_write_pair(idx, 0, 0); for (k = 0; k < 3; k++) res[0][k] = run(k); have[0] = 1;
        wc_write_pair(idx, keep.base, keep.mask);
    }
    memset(&r, 0, sizeof r); r.x.ax = 0x0003; __dpmi_int(0x10, &r);
    __dpmi_free_ldt_descriptor(g_sel); __dpmi_free_physical_address_mapping(&map);
    snprintf(line, sizeof line, "WCSET %s /BENCH: %s fam %d model %d, VESA mode %Xh %dx%dx%d, LFB %08lX, %lu MB",
             PACKVER_STR, WC.vendor[0] ? WC.vendor : "?", WC.family, WC.model, best, bw, h, bb, phys, WC.vram >> 20);
    puts(line);
    { char p[300]; snprintf(p, sizeof p, "%s\\WCBENCH.TXT", WC.home); f = fopen(p, "a"); }
    if (f) fprintf(f, "%s\r\n", line);
    printf("%-27s %12s %12s\n", "MB/s", "WC off", "WC on");
    for (k = 0; k < 3; k++)
    {
        char a[16] = "-", b[16] = "-";
        if (have[0]) snprintf(a, sizeof a, "%.1f", res[0][k]);
        if (have[1]) snprintf(b, sizeof b, "%.1f", res[1][k]);
        snprintf(line, sizeof line, "%-27s %12s %12s%s", name[k], a, b, have[0] && have[1] && res[0][k] > 0 ?
                 (snprintf(msg, sizeof msg, "   x%.2f", res[1][k] / res[0][k]), msg) : "");
        puts(line); if (f) fprintf(f, "%s\r\n", line);
    }
    if (!have[0] || !have[1])
    {
        snprintf(line, sizeof line, "Only one column: %s", !WC.ok ? WC.why : st < 0 ? "a BIOS MTRR covers the frame buffer" :
                 st == 1 ? "WC was set by someone else (left alone)" : "no free MTRR");
        puts(line); if (f) fprintf(f, "%s\r\n", line);
    }
    if (f) { fprintf(f, "\r\n"); fclose(f); }
    return 0;
}

int main(int argc, char** argv)
{
    char home[260], msg[160]; int i, cmd = 0, ask = 0, b = 0, r = 0;
    { char* e; _fixpath(argv[0], home); e = strrchr(home, '/'); if (e) *e = 0; else strcpy(home, ".");
      for (e = home; *e; e++) if (*e == '/') *e = '\\';
      wc_set_home(home); }
    wc_load_setting();
    for (i = 1; i < argc; i++)
    {
        const char* a = argv[i];
        if (!stricmp(a, "LIST")) cmd = 0;
        else if (!stricmp(a, "ON")) cmd = 1;
        else if (!stricmp(a, "OFF")) cmd = 2;
        else if (!stricmp(a, "AUTO")) cmd = 3;
        else if (!stricmp(a, "/BENCH")) b = 1;
        else if (!stricmp(a, "/ASK")) ask = 1;
        else
        {
            puts("WCSET [LIST|ON|OFF|AUTO] [/BENCH] [/ASK] - write-combining on the VESA frame buffer (MTRR)\n"
                 "  LIST  show CPU, MTRRs, frame buffer and the setting (default)\n"
                 "  ON    WC on now, until reboot (games too); MENU/MIDPLAY setting ON\n"
                 "  OFF   WCSET's WC off now; MENU/MIDPLAY leave the MTRRs alone\n"
                 "  AUTO  MENU/MIDPLAY switch WC on while they run (P6-class CPUs), restore it at exit\n"
                 "  /BENCH  frame buffer write speed with WC off and on (to WCBENCH.TXT)\n"
                 "Setting in WCSET.CFG; SET WCSET=OFF overrides it. Not in a Windows DOS box.");
            return 2;
        }
    }
    if (wc_in_windows()) { puts("WCSET: not in a Windows DOS box (Windows' display driver handles write-combining)."); return 1; }
    if (cmd == 1) { wc_save_setting(WC_ON); r = !wc_enable(1, msg, sizeof msg); printf("Write-combining: %s\n", msg); wc_log("WCSET", msg); }
    else if (cmd == 2) { wc_save_setting(WC_OFF); r = !wc_disable(msg, sizeof msg); printf("Write-combining: %s\n", msg); wc_log("WCSET", msg); }
    else if (cmd == 3) { wc_save_setting(WC_AUTO); puts("Setting: AUTO (MENU and MIDPLAY switch WC on while they run)."); }
    if (b) return bench();
    if (cmd == 0 || ask) list();
    if (ask)
    {
        int c;
        printf("\nSetting: A = AUTO (recommended)  O = ON (also for games, until reboot)  F = OFF  Esc = keep %s ", wc_mode_name(WC.mode));
        fflush(stdout);
        c = toupper(getch());
        if (c == 'A') { wc_save_setting(WC_AUTO); puts("\nAUTO saved."); }
        else if (c == 'O') { wc_save_setting(WC_ON); wc_enable(1, msg, sizeof msg); printf("\nON saved. Write-combining: %s\n", msg); wc_log("WCSET", msg); }
        else if (c == 'F') { wc_save_setting(WC_OFF); wc_disable(msg, sizeof msg); printf("\nOFF saved. %s\n", msg); wc_log("WCSET", msg); }
        else puts("");
    }
    if (cmd == 0 && !ask) return WC.ok ? 0 : 1;
    return r;
}
