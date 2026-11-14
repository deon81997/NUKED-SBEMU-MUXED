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

/* ---- USB MIDI (SBEMU /MUSB, beta.121): AMIS function 05h. sbemu_musb() -> 1 and m filled when SBEMU answered (any
 * state, also "off"), 0 when SBEMU is not found or older than beta.121. One text the same everywhere: musb_line(). */
typedef struct
{
    int on;                 /* 0 /MUSB off, 1 on, 2 asked for but SBEMU's USB is off (/JOY:0 or /USB:0) */
    char state[16];         /* waiting / active / unplugged / dropped / off */
    char id[12], name[40], port[48], last[80], mpu[16];
    unsigned long sent, drop, err, in, q, busy, btmo, ign;
    int cap;
} SBMUSB;
static __attribute__((unused)) unsigned long sbc_num(const char* s, const char* key)
{
    char v[16]; sbc_field(s, key, v, sizeof v); return strtoul(v, NULL, 10);
}
static __attribute__((unused)) void sbc_part(const char* s, const char* key, char* out, int size)
{
    const char* p = strstr(s, key); int n = 0;
    out[0] = 0;
    if (!p) return;
    p += strlen(key);
    while (p[n] && p[n] != '|' && n < size - 1) { out[n] = p[n]; n++; }
    out[n] = 0;
}
static __attribute__((unused)) int sbemu_musb(SBMUSB* m)
{
    char t[400];
    int id = sbc_amis(t, 2);                 /* only the multiplex id */
    __dpmi_regs r;
    memset(m, 0, sizeof *m);
    if (id <= 0) return 0;
    memset(&r, 0, sizeof r); r.h.ah = (unsigned char)id; r.h.al = 0x05;
    r.d.ebx = __tb; r.d.ecx = sizeof t;
    { char z = 0; dosmemput(&z, 1, __tb); }
    if (__dpmi_int(0x2D, &r) != 0 || r.h.al != 0 || (r.x.flags & 1)) return 0;
    dosmemget(__tb, sizeof t, t); t[sizeof t - 1] = 0;
    if (strncmp(t, "MUSB=", 5)) return 0;
    m->on = (int)sbc_num(t, "MUSB=");
    sbc_field(t, "STATE=", m->state, sizeof m->state); sbc_field(t, "ID=", m->id, sizeof m->id);
    sbc_field(t, "MPU=", m->mpu, sizeof m->mpu);
    m->sent = sbc_num(t, "SENT="); m->drop = sbc_num(t, "DROP="); m->err = sbc_num(t, "ERR="); m->in = sbc_num(t, " IN=");
    m->q = sbc_num(t, " Q="); m->busy = sbc_num(t, "BUSY="); m->btmo = sbc_num(t, "BTMO="); m->ign = sbc_num(t, "IGN=");
    m->cap = (int)sbc_num(t, "CAP=");
    sbc_part(t, "|NAME=", m->name, sizeof m->name); sbc_part(t, "|PORT=", m->port, sizeof m->port); sbc_part(t, "|LAST=", m->last, sizeof m->last);
    return 1;
}
/* "USB MIDI: M-Audio USB Uno (0763:0150) on UHCI 1 port 2, active, 1234 sent" - or why not */
static __attribute__((unused)) const char* musb_line(const SBMUSB* m, char* out, int size)
{
    if (m->on == 0) snprintf(out, size, "USB MIDI: off (SBEMU without /MUSB:1)");
    else if (m->on == 2) snprintf(out, size, "USB MIDI: asked for, but SBEMU's USB is off (/JOY:0 or /USB:0)");
    else if (!strcmp(m->state, "waiting")) snprintf(out, size, "USB MIDI: on, no device plugged in yet");
    else snprintf(out, size, "USB MIDI: %s (%s) on %s, %s, %lu sent%s%lu dropped%s%s", m->name[0] ? m->name : "device", m->id, m->port,
        !strcmp(m->state, "active") ? "active" : !strcmp(m->state, "unplugged") ? "unplugged (inactive)" : "dropped after errors (inactive)",
        m->sent, ", ", m->drop, m->last[0] ? ", last error: " : "", m->last);
    return out;
}
#endif
