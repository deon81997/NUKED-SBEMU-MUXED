/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* mpuport.h - find SBEMU's MPU-401, whatever port it was set to.
 *
 * mpu_probe(port)  1 if an MPU-401 answers a reset at that port.
 * mpu_find(pref)   the port SBEMU's MPU-401 answers at: pref first (the saved /P, 0 = none), then the
 *                  /P value of BLASTER, then every port SBSET offers. 0 = none answers.
 *
 * The probe copes with what used to make the menu say "Not loaded" while SBEMU was running:
 *  - a report another tool asked for (MPU commands 0xB0, 0xA6, 0xAF) and did not read to the end:
 *    the bytes waiting at the data port are read away first;
 *  - an MPU-401 left in UART mode: a real one does not answer the first reset there, so a second
 *    reset follows when the first gets no ACK;
 *  - the ACK coming after other bytes: up to 16 bytes are read looking for 0xFE.
 * Nothing is sent to the synthesizer (resets go to the command port only). */
#ifndef MPUPORT_INCLUDED
#define MPUPORT_INCLUDED
#include <pc.h>
#include <stdlib.h>
#include <ctype.h>

static __attribute__((unused)) const int mpu_ports[] = { 0x330, 0x300, 0x310, 0x320, 0x332, 0x334 };

static __attribute__((unused)) int mpu_drain(int port)
{
    int n = 0;
    while (n < 400 && !(inportb(port + 1) & 0x80)) { inportb(port); n++; }
    return n;
}
static __attribute__((unused)) int mpu_reset_ack(int port)
{
    long i; int got = 0;
    outportb(port + 1, 0xFF);
    for (i = 0; i < 100000L && got < 16; ++i)
        if (!(inportb(port + 1) & 0x80)) { got++; if (inportb(port) == 0xFE) return 1; i = 0; }
    return 0;
}
static __attribute__((unused)) int mpu_probe(int port)
{
    unsigned char st = inportb(port + 1);
    if (st == 0xFF && inportb(port) == 0xFF) return 0;   /* nothing there (floating bus) */
    mpu_drain(port);
    if (mpu_reset_ack(port)) { mpu_drain(port); return 1; }
    if (mpu_reset_ack(port)) { mpu_drain(port); return 1; } /* it was in UART mode */
    return 0;
}
/* /P value of a BLASTER string (SBEMU writes P<port> there), 0 if none */
static __attribute__((unused)) int mpu_from_blaster(const char* b)
{
    const char* s;
    if (!b) return 0;
    for (s = b; *s; s++)
        if ((s == b || s[-1] == ' ') && toupper((unsigned char)*s) == 'P' && isxdigit((unsigned char)s[1]))
            return (int)strtol(s + 1, NULL, 16);
    return 0;
}
static __attribute__((unused)) int mpu_find(int pref)
{
    int bp = mpu_from_blaster(getenv("BLASTER"));
    unsigned i;
    if (pref && mpu_probe(pref)) return pref;
    if (bp && bp != pref && mpu_probe(bp)) return bp;
    for (i = 0; i < sizeof mpu_ports / sizeof mpu_ports[0]; i++)
        if (mpu_ports[i] != pref && mpu_ports[i] != bp && mpu_probe(mpu_ports[i])) return mpu_ports[i];
    return 0;
}
#endif
