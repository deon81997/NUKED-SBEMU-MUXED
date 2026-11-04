/* miditest.c - plays through the "SC-55 PACK" MIDI device and the MIDI
 * Mapper; writes C:\MIDITEST.LOG */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>

static FILE *lg;
static void play(HMIDIOUT h, int base)
{
    int n[4] = {0, 4, 7, 12}, i;
    midiOutShortMsg(h, 0x0000C0);                 /* piano */
    for (i = 0; i < 4; i++) {
        midiOutShortMsg(h, 0x90 | ((base + n[i]) << 8) | (100 << 16));
        Sleep(400);
        midiOutShortMsg(h, 0x80 | ((base + n[i]) << 8));
        Sleep(100);
    }
}
int main(void)
{
    UINT i, n = midiOutGetNumDevs(), dev = (UINT)-2;
    MMRESULT r;
    HMIDIOUT h;
    lg = fopen("C:\\MIDITEST.LOG", "w");
    fprintf(lg, "%u MIDI out devices\n", n);
    for (i = 0; i < n; i++) {
        MIDIOUTCAPSA c;
        midiOutGetDevCapsA(i, &c, sizeof c);
        fprintf(lg, "  %u: '%s' tech %u\n", i, c.szPname, c.wTechnology);
        if (!strcmp(c.szPname, "SC-55 PACK")) dev = i;
    }
    if (dev != (UINT)-2) {
        static char gs[] = {(char)0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, (char)0xF7};
        MIDIHDR hd;
        r = midiOutOpen(&h, dev, 0, 0, CALLBACK_NULL);
        fprintf(lg, "open SC-55 PACK -> %u\n", r);
        if (!r) {
            memset(&hd, 0, sizeof hd);
            hd.lpData = gs; hd.dwBufferLength = sizeof gs;
            midiOutPrepareHeader(h, &hd, sizeof hd);
            r = midiOutLongMsg(h, &hd, sizeof hd);
            fprintf(lg, "sysex -> %u, flags %lx\n", r, hd.dwFlags);
            Sleep(300);
            midiOutUnprepareHeader(h, &hd, sizeof hd);
            play(h, 60);
            midiOutReset(h);
            midiOutClose(h);
        }
    }
    r = midiOutOpen(&h, MIDI_MAPPER, 0, 0, CALLBACK_NULL);
    fprintf(lg, "open mapper -> %u\n", r);
    if (!r) { Sleep(500); play(h, 72); midiOutClose(h); }
    fprintf(lg, "done\n");
    fclose(lg);
    return 0;
}
