/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* crit24.h - drive checks for the folder browsers (empty CD drive, card reader without a card, unplugged
 * USB drive must just fail, never stop with "Abort, Retry, Fail?").
 * GPL-2.0-or-later. */
#ifndef CRIT24_H
#define CRIT24_H
#include <dpmi.h>
#include <go32.h>
#include <sys/movedata.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
/* No INT 24h handler of our own: setting the real-mode INT 24h vector under HDPMI32I locked up the host
 * (tested in 86Box). The DPMI host already answers critical errors with "Fail" for a protected-mode program,
 * so access() on an empty drive simply returns an error. */
static void crit_on(void) {}
static void crit_off(void) {}
/* CD drives (MSCDEX/SHSUCDX, e.g. the empty uCDD drive M:) are asked through INT 2Fh first: an empty one
 * is skipped without DOS touching it. 1 = a CD drive with a disc, 0 = empty, -1 = not a CD drive */
static int crit_cd_ready(int letter)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x150B; r.x.cx = (unsigned short)(toupper(letter) - 'A');
    __dpmi_int(0x2F, &r);
    if (r.x.bx != 0xADAD || r.x.ax == 0) return -1;
    memset(&r, 0, sizeof r);                 /* read the volume descriptor (VTOC) into the transfer buffer */
    r.x.ax = 0x1505; r.x.cx = (unsigned short)(toupper(letter) - 'A'); r.x.dx = 0;
    r.x.es = (unsigned short)(__tb >> 4); r.x.bx = (unsigned short)(__tb & 15);
    __dpmi_int(0x2F, &r);
    return (r.x.flags & 1) ? 0 : 1;
}
/* 1 if the folder (or drive root, "D:" or "D:\\") can be opened; an empty or unreadable drive just fails */
static int crit_dir_ok(const char* d)
{
    char p[200];
    size_t n = strlen(d);
    if (n >= 2 && d[1] == ':' && crit_cd_ready(d[0]) == 0) return 0;
    snprintf(p, sizeof p, "%s%s", d, n == 2 ? "\\" : "");
    if (strlen(p) > 3 && p[strlen(p) - 1] == '\\') p[strlen(p) - 1] = 0;
    return access(p, D_OK) == 0;
}
#endif
