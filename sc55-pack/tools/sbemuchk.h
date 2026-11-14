/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* sbemuchk.h - is SBEMU loaded? Two independent ways, so one that cannot see SBEMU does not say "not loaded":
 *  1. AMIS (INT 2Dh, real mode): SBEMU's signature, then (beta.117+) function 04h = "VER=.. MODE=.. MPU=.. TRAP=..".
 *     Needs no port trap, so it works whichever DPMI host / memory manager the caller runs under.
 *  2. MPU-401 probe (mpuport.h): only answers when SBEMU's port trap reaches this program.
 * sbemu_check() -> SBC_LOADED (either way found it), SBC_NOT (neither), SBC_UNKNOWN (neither, but the AMIS scan
 * could not run or this is a Windows DOS box, where a SBEMU loaded before Windows is not visible). */
#ifndef SBEMUCHK_INCLUDED
#define SBEMUCHK_INCLUDED
#include <dpmi.h>
#include <go32.h>
#include <sys/movedata.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "mpuport.h"

enum { SBC_NOT = 0, SBC_LOADED = 1, SBC_UNKNOWN = 2 };
typedef struct
{
    int state;          /* SBC_* */
    int amis;           /* AMIS multiplex id, 0 = no SBEMU signature, -1 = the scan failed */
    int mpu;            /* port the MPU-401 answered at, 0 = none */
    char info[160];     /* function 04h line ("" = older SBEMU or not found) */
    char ver[48], mode[12], trap[12];
} SBCHK;

static __attribute__((unused)) void sbc_field(const char* s, const char* key, char* out, int size)
{
    const char* p = strstr(s, key);
    int n = 0;
    out[0] = 0;
    if (!p) return;
    p += strlen(key);
    while (p[n] && p[n] != ' ' && n < size - 1) { out[n] = p[n]; n++; }
    out[n] = 0;
}

static __attribute__((unused)) int sbc_amis(char* info, int size)
{
    static const char sig[] = "Crazii  SBEMU   ";
    int id;
    info[0] = 0;
    for (id = 1; id <= 0xFF; ++id)
    {
        __dpmi_regs r; char got[16];
        memset(&r, 0, sizeof r); r.h.ah = (unsigned char)id; r.h.al = 0;
        if (__dpmi_int(0x2D, &r) != 0) return -1;
        if (r.h.al != 0xFF) continue;
        dosmemget((unsigned long)r.x.dx * 16 + r.x.di, 16, got);
        if (memcmp(got, sig, 16)) continue;
        memset(&r, 0, sizeof r); r.h.ah = (unsigned char)id; r.h.al = 0x04;
        r.d.ebx = __tb; r.d.ecx = (unsigned long)(size < 160 ? size : 160);
        { char z = 0; dosmemput(&z, 1, __tb); }
        if (__dpmi_int(0x2D, &r) == 0 && r.h.al == 0 && !(r.x.flags & 1)) { dosmemget(__tb, size, info); info[size - 1] = 0; }
        return id;
    }
    return 0;
}

static __attribute__((unused)) void sbemu_check(SBCHK* c, int pref, int windows)
{
    memset(c, 0, sizeof *c);
    c->amis = sbc_amis(c->info, sizeof c->info);
    if (c->info[0])
    {
        int p;
        sbc_field(c->info, "VER=", c->ver, sizeof c->ver);
        sbc_field(c->info, "MODE=", c->mode, sizeof c->mode);
        sbc_field(c->info, "TRAP=", c->trap, sizeof c->trap);
        { char m[8]; sbc_field(c->info, "MPU=", m, sizeof m); p = (int)strtol(m, NULL, 16); }
        if (p >= 0x200 && p <= 0x3FF) pref = p;
    }
    c->mpu = mpu_find(pref);
    if (c->amis > 0 || c->mpu) c->state = SBC_LOADED;
    else if (windows || c->amis < 0) c->state = SBC_UNKNOWN;
    else c->state = SBC_NOT;
}
#endif
