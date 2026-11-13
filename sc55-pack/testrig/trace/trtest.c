/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU. Build: i586-pc-msdosdjgpp-gcc -O2 -o TRTEST.EXE trtest.c -s */
/* TRTEST - sends known DSP / MPU-401 commands so SBEMU's flight recorder has something to show */
#include <stdio.h>
#include <pc.h>
#include <dos.h>
static void dsp_w(int b) { int i; for (i = 0; i < 100000 && (inportb(0x22C) & 0x80); i++); outportb(0x22C, b); }
static void mpu_c(int b) { outportb(0x331, b); delay(2); inportb(0x330); }
static void mpu_d(int b) { outportb(0x330, b); }
int main(void)
{
    int i;
    outportb(0x226, 1); delay(1); outportb(0x226, 0); delay(1); printf("reset ack %02X\n", inportb(0x22A));
    dsp_w(0xE1); printf("DSP version %d.", inportb(0x22A)); printf("%d\n", inportb(0x22A));
    dsp_w(0xD1);
    dsp_w(0x40); dsp_w(0xA5);
    dsp_w(0x80); dsp_w(0x10); dsp_w(0x00);          /* silence DAC: not emulated */
    dsp_w(0x24); dsp_w(0x10); dsp_w(0x00);          /* 8-bit record: not emulated */
    for (i = 0; i < 20; i++) { dsp_w(0xF2); delay(30); }   /* IRQ, never acknowledged */
    dsp_w(0x14); dsp_w(0xFF); dsp_w(0x0F);          /* 4 KB single-cycle without a DMA set up */
    delay(500);
    mpu_c(0xFF); mpu_c(0x3F); mpu_c(0xAC);          /* reset, UART, intelligent-mode version query */
    mpu_d(0xC0); mpu_d(0x10);
    for (i = 0; i < 30; i++) { mpu_d(0x90); mpu_d(60 + i % 12); mpu_d(100); delay(20); mpu_d(0x80); mpu_d(60 + i % 12); mpu_d(0); }
    mpu_d(0xF0); mpu_d(0x41); mpu_d(0x10); mpu_d(0x42); mpu_d(0x12); mpu_d(0x40); mpu_d(0x00); mpu_d(0x7F); mpu_d(0x00); mpu_d(0x41); mpu_d(0xF7);
    mpu_d(0xE0); mpu_d(0); mpu_d(0x40);
    printf("TRTEST done\n");
    return 0;
}
