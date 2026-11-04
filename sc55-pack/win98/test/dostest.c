/* dostest.c - exercise a Sound Blaster 16 at 220h/IRQ5/DMA1/HDMA5,
 * AdLib at 388h and an MPU-401 at 330h the way DOS games do.
 * Writes results to C:\DOSTEST.LOG. Open Watcom, 16-bit DOS, small model. */
#include <stdio.h>
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <string.h>
#include <math.h>

#define SB 0x220
#define IRQ 5
#define VEC (8 + IRQ)

static FILE *lg;
static volatile unsigned irqs8, irqs16;
static void (__interrupt __far *oldvec)(void);

static unsigned long ticks(void) { return *(volatile unsigned long __far *)MK_FP(0x40, 0x6C); }

static void __interrupt __far sb_isr(void)
{
    unsigned char st;
    outp(SB + 4, 0x82);
    st = inp(SB + 5);
    if (st & 1) { inp(SB + 0xE); irqs8++; }
    if (st & 2) { inp(SB + 0xF); irqs16++; }
    if (!(st & 3)) { inp(SB + 0xE); irqs8++; }   /* pre-SB16 style */
    outp(0x20, 0x20);
}

static int dsp_wait_write(void) { long n = 100000; while ((inp(SB + 0xC) & 0x80) && --n); return n != 0; }
static void dsp_w(unsigned char v) { dsp_wait_write(); outp(SB + 0xC, v); }
static int dsp_r(void)
{
    long n = 100000;
    while (!(inp(SB + 0xE) & 0x80) && --n);
    if (!n) return -1;
    return inp(SB + 0xA);
}

static int dsp_reset(void)
{
    int i;
    outp(SB + 6, 1);
    for (i = 0; i < 100; i++) inp(SB + 6);
    outp(SB + 6, 0);
    return dsp_r();
}

/* DMA buffer: 64 KB static area, aligned so it does not cross a 64K page */
static unsigned char far *dmabuf;
static unsigned long dmaphys;
static unsigned char rawbuf[40000];

static void setup_buffer(void)
{
    unsigned long p = ((unsigned long)FP_SEG(rawbuf) << 4) + FP_OFF(rawbuf);
    if ((p & 0xFFFF) + 16384 > 0x10000UL) p = (p + 0xFFFF) & ~0xFFFFUL;
    dmaphys = p;
    dmabuf = (unsigned char far *)MK_FP((unsigned)(p >> 4), (unsigned)(p & 15));
}

static void dma8(unsigned len, int autoinit)
{
    outp(0x0A, 4 | 1);
    outp(0x0C, 0);
    outp(0x0B, (autoinit ? 0x58 : 0x48) | 1);
    outp(0x02, (unsigned char)dmaphys); outp(0x02, (unsigned char)(dmaphys >> 8));
    outp(0x83, (unsigned char)(dmaphys >> 16));
    outp(0x03, (unsigned char)(len - 1)); outp(0x03, (unsigned char)((len - 1) >> 8));
    outp(0x0A, 1);
}

static void dma16(unsigned words, int autoinit)
{
    unsigned long w = (dmaphys >> 1) & 0xFFFF;
    outp(0xD4, 4 | 1);
    outp(0xD8, 0);
    outp(0xD6, (autoinit ? 0x58 : 0x48) | 1);
    outp(0xC4, (unsigned char)w); outp(0xC4, (unsigned char)(w >> 8));
    outp(0x8B, (unsigned char)(dmaphys >> 16) & 0xFE);
    outp(0xC6, (unsigned char)(words - 1)); outp(0xC6, (unsigned char)((words - 1) >> 8));
    outp(0xD4, 1);
}

static unsigned dma_count(int ch16)
{
    unsigned lo, hi;
    if (ch16) { outp(0xD8, 0); lo = inp(0xC6); hi = inp(0xC6); }
    else { outp(0x0C, 0); lo = inp(0x03); hi = inp(0x03); }
    return lo | (hi << 8);
}

static void wait_ticks(unsigned n) { unsigned long t = ticks(); while (ticks() - t < n); }

int main(void)
{
    int v, i, hi, lo;
    unsigned long t;
    char cr[64];
    lg = fopen("C:\\DOSTEST.LOG", "w");
    if (!lg) return 1;
    setup_buffer();
    fprintf(lg, "dma buffer at %05lx\n", dmaphys);

    /* 1. reset */
    v = dsp_reset();
    fprintf(lg, "reset -> %02x (expect aa)\n", v & 0xFF);

    /* 2. version */
    dsp_w(0xE1); hi = dsp_r(); lo = dsp_r();
    fprintf(lg, "version %d.%02d\n", hi, lo);

    /* 3. copyright */
    dsp_w(0xE3);
    for (i = 0; i < 60; i++) { v = dsp_r(); if (v <= 0) break; cr[i] = (char)v; }
    cr[i] = 0;
    fprintf(lg, "copyright '%s'\n", cr);

    /* 4. invert + test register */
    dsp_w(0xE0); dsp_w(0x5A); v = dsp_r();
    fprintf(lg, "E0 5A -> %02x (expect a5)\n", v & 0xFF);
    dsp_w(0xE4); dsp_w(0x3C); dsp_w(0xE8); v = dsp_r();
    fprintf(lg, "test reg -> %02x (expect 3c)\n", v & 0xFF);

    /* 5. mixer IRQ/DMA config */
    outp(SB + 4, 0x80); v = inp(SB + 5);
    fprintf(lg, "mixer 80h = %02x (IRQ5 = 02)\n", v);
    outp(SB + 4, 0x81); v = inp(SB + 5);
    fprintf(lg, "mixer 81h = %02x (DMA1+HDMA5 = 22)\n", v);

    /* IRQ handler */
    oldvec = _dos_getvect(VEC);
    _dos_setvect(VEC, sb_isr);
    outp(0x21, inp(0x21) & ~(1 << IRQ));

    /* 6. F2 interrupt */
    irqs8 = 0;
    dsp_w(0xF2);
    t = ticks(); while (!irqs8 && ticks() - t < 5);
    fprintf(lg, "F2 -> %u irq(s) after %lu ticks\n", irqs8, ticks() - t);

    /* 7. 8-bit single-cycle, 22050 Hz, 22050 bytes = 1 s */
    for (i = 0; i < 16384; i++) dmabuf[i] = (unsigned char)(128 + 100 * sin(i * 2 * 3.14159 * 441.0 / 22050.0));
    irqs8 = 0;
    dma8(16384, 0);
    dsp_w(0xD1);
    dsp_w(0x40); dsp_w(256 - 1000000UL / 22050);
    dsp_w(0x14); dsp_w((16384 - 1) & 0xFF); dsp_w((16384 - 1) >> 8);
    t = ticks();
    wait_ticks(4);
    fprintf(lg, "8-bit: dma count after 4 ticks = %u (of 16383)\n", dma_count(0));
    while (!irqs8 && ticks() - t < 40);
    fprintf(lg, "8-bit single 16384 bytes @22050: irq after %lu ticks (expect ~13)\n", ticks() - t);

    /* 8. 16-bit auto-init stereo 44100, block 4096 words */
    for (i = 0; i < 8192; i++) {
        int s = (int)(12000 * sin(i / 2 * 2 * 3.14159 * 882.0 / 44100.0));
        ((int far *)dmabuf)[i] = (i & 1) ? s / 2 : s;
    }
    irqs16 = 0;
    dma16(8192, 1);
    dsp_w(0x41); dsp_w(44100 >> 8); dsp_w(44100 & 0xFF);
    dsp_w(0xB6); dsp_w(0x30); dsp_w((4096 - 1) & 0xFF); dsp_w((4096 - 1) >> 8);
    t = ticks();
    wait_ticks(36);   /* ~2 s */
    fprintf(lg, "16-bit auto stereo 44100, block 4096 words: %u irqs in 36 ticks (expect ~43)\n", irqs16);
    fprintf(lg, "16-bit dma count now %u\n", dma_count(1));
    dsp_w(0xD9);
    wait_ticks(4);
    fprintf(lg, "after D9: %u irqs\n", irqs16);

    /* 9. 8-bit auto-init with 48h block size, then DA */
    irqs8 = 0;
    dma8(8192, 1);
    dsp_w(0x40); dsp_w(256 - 1000000UL / 11025);
    dsp_w(0x48); dsp_w((2048 - 1) & 0xFF); dsp_w((2048 - 1) >> 8);
    dsp_w(0x1C);
    wait_ticks(18);
    fprintf(lg, "8-bit auto 11025 block 2048: %u irqs in 18 ticks (expect ~5)\n", irqs8);
    dsp_w(0xDA);
    wait_ticks(5);

    outp(0x21, inp(0x21) | (1 << IRQ));
    _dos_setvect(VEC, oldvec);

    /* 10. AdLib detection */
    outp(0x388, 4); inp(0x388); outp(0x389, 0x60);
    outp(0x388, 4); inp(0x388); outp(0x389, 0x80);
    v = inp(0x388);
    outp(0x388, 2); outp(0x389, 0xFF);
    outp(0x388, 4); outp(0x389, 0x21);
    for (i = 0; i < 200; i++) inp(0x388);
    wait_ticks(1);
    lo = inp(0x388);
    outp(0x388, 4); outp(0x389, 0x60);
    outp(0x388, 4); outp(0x389, 0x80);
    fprintf(lg, "adlib status %02x then %02x (expect 00 then c0) -> %s\n", v & 0xE0, lo & 0xE0,
            ((v & 0xE0) == 0 && (lo & 0xE0) == 0xC0) ? "found" : "not found");
    /* a note on the OPL */
    outp(0x388, 0x20); outp(0x389, 0x01);
    outp(0x388, 0xA0); outp(0x389, 0x98);
    outp(0x388, 0xB0); outp(0x389, 0x31);

    /* 11. MPU-401 */
    outp(0x331, 0xFF);
    t = ticks(); while ((inp(0x331) & 0x80) && ticks() - t < 3);
    v = inp(0x330);
    outp(0x331, 0x3F);
    t = ticks(); while ((inp(0x331) & 0x80) && ticks() - t < 3);
    lo = inp(0x330);
    fprintf(lg, "mpu reset ack %02x, uart ack %02x (expect fe fe)\n", v, lo);
    outp(0x330, 0x90); outp(0x330, 60); outp(0x330, 100);
    wait_ticks(5);
    outp(0x330, 0x80); outp(0x330, 60); outp(0x330, 0);

    fprintf(lg, "done\n");
    fclose(lg);
    return 0;
}
