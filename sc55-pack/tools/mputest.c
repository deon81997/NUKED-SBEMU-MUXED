/* MPUTEST - plays a short test tune through the MPU-401 at port 330h, i.e. through
 * the running SBEMU + SC-55 emulation. SBEMU must already be loaded (run SC55.BAT).
 *
 *   MPUTEST [/P:330] [/L]        /P: MPU base port in hex   /L loop until a key is pressed
 *
 * GPL-2.0-or-later. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pc.h>
#include <conio.h>
#include <time.h>
#include <dos.h>
#include "mpuport.h"

static int base = 0x330;

static void wait_ms(int ms)
{
    uclock_t end = uclock() + (uclock_t)ms * UCLOCKS_PER_SEC / 1000;
    while (uclock() < end) {}
}

static void put(unsigned char b)
{
    /* wait while the interface is busy (DRR, status bit 6), with a timeout */
    uclock_t end = uclock() + UCLOCKS_PER_SEC;
    while ((inportb(base + 1) & 0x40) && uclock() < end) {}
    outportb(base, b);
}

static void msg3(int s, int a, int b) { put((unsigned char)s); put((unsigned char)a); put((unsigned char)b); }
static void msg2(int s, int a) { put((unsigned char)s); put((unsigned char)a); }

static int detect(void)
{
    uclock_t end;
    outportb(base + 1, 0xFF);                       /* reset */
    end = uclock() + UCLOCKS_PER_SEC / 2;
    while (uclock() < end)
        if (!(inportb(base + 1) & 0x80))            /* DSR low: a byte is waiting */
            return inportb(base) == 0xFE;
    return 0;
}

static void tune(void)
{
    static const unsigned char scale[] = {60, 62, 64, 65, 67, 69, 71, 72};
    static const unsigned char gs_reset[] = {0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7};
    int i, n;
    for (i = 0; i < (int)sizeof gs_reset; i++) put(gs_reset[i]);
    wait_ms(300);
    msg2(0xC0, 0);   /* piano */
    msg2(0xC1, 48);  /* strings */
    msg2(0xC2, 33);  /* finger bass */
    printf("  piano scale\n");
    for (n = 0; n < 8; n++)
    {
        msg3(0x90, scale[n], 100);
        wait_ms(260);
        msg3(0x80, scale[n], 0);
        wait_ms(60);
    }
    printf("  chord with strings and bass\n");
    msg3(0x90, 60, 96); msg3(0x90, 64, 96); msg3(0x90, 67, 96); msg3(0x90, 72, 96);
    msg3(0x91, 48, 80); msg3(0x91, 55, 80);
    msg3(0x92, 36, 110);
    wait_ms(1800);
    for (n = 0; n < 4; n++) { static const unsigned char c[] = {60, 64, 67, 72}; msg3(0x80, c[n], 0); }
    msg3(0x81, 48, 0); msg3(0x81, 55, 0); msg3(0x82, 36, 0);
    printf("  drums\n");
    for (n = 0; n < 8; n++)
    {
        msg3(0x99, 42, 90);
        if (n % 4 == 0) msg3(0x99, 36, 120);
        if (n % 4 == 2) msg3(0x99, 38, 110);
        wait_ms(300);
    }
    wait_ms(800);
    msg3(0xB0, 123, 0); msg3(0xB1, 123, 0); msg3(0xB2, 123, 0); msg3(0xB9, 123, 0); /* all notes off */
}

int main(int argc, char** argv)
{
    int loop = 0, i;
    for (i = 1; i < argc; i++)
    {
        if (!strncmp(argv[i], "/P:", 3) || !strncmp(argv[i], "/p:", 3)) base = (int)strtol(argv[i] + 3, NULL, 16);
        else if (!strcmp(argv[i], "/L") || !strcmp(argv[i], "/l")) loop = 1;
        else { puts("MPUTEST [/P:330] [/L]\nPlays a test tune through the MPU-401 (SBEMU + SC-55 must be loaded)."); return 2; }
    }
    { int p = mpu_find(base); if (p) base = p; }   /* SBEMU's MPU-401 on whatever port it uses */
    printf("MPU-401 at port %Xh: ", base);
    if (!detect())
    {
        puts("no answer.\nIs SBEMU loaded (SC55.BAT) with the SC-55 enabled, and is this a protected-mode program\n"
             "run after HDPMI32I -r? Real hardware ports without SBEMU will not answer either.");
        return 1;
    }
    puts("found. Playing test tune...");
    outportb(base + 1, 0x3F);                       /* UART mode */
    do tune(); while (loop && !kbhit());
    if (loop && kbhit()) getch();
    puts("Done. No sound? Check volume, the SC55LOG.TXT result and that the card is the one SBEMU uses.");
    return 0;
}
