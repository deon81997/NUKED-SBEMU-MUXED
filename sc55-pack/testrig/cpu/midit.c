/* midit.c - MIDIT.EXE: wake-up test for SC55W's sleeping sound output (test rig, Windows 9x).
 * MIDIT count gap_ms [log]: on the "NUKED SB-EMU" MIDI device, `count` times: piano C5 for 500 ms, then
 * `gap_ms` of silence (long enough for the output to go to sleep). Appends start times (GetTickCount) to log.
 * GUI subsystem (-mwindows): a console window would be a DOS box to SC55W. */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int WINAPI WinMain(HINSTANCE i, HINSTANCE p, LPSTR cl, int sh)
{
    int count = 3, gap = 6000, n = midiOutGetNumDevs(), dev = -1, k;
    char log[260] = "D:\\MIDIT.LOG";
    HMIDIOUT mo;
    FILE *f;
    (void)i; (void)p; (void)sh;
    sscanf(cl, "%d %d %259s", &count, &gap, log);
    for (k = 0; k < n; k++) {
        MIDIOUTCAPSA c;
        if (midiOutGetDevCapsA(k, &c, sizeof c) == MMSYSERR_NOERROR && strstr(c.szPname, "NUKED")) dev = k;
    }
    f = fopen(log, "a");
    if (dev < 0 || midiOutOpen(&mo, dev, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        if (f) { fprintf(f, "no NUKED MIDI device (%d devices)\n", n); fclose(f); }
        return 1;
    }
    midiOutShortMsg(mo, 0x0000C0);              /* program 0 */
    for (k = 0; k < count; k++) {
        Sleep(gap);
        if (f) fprintf(f, "note %d at %lu\n", k, GetTickCount());
        midiOutShortMsg(mo, 0x7F4890);          /* note on C5 vel 127 */
        Sleep(500);
        midiOutShortMsg(mo, 0x004880);          /* note off */
    }
    Sleep(gap);
    midiOutClose(mo);
    if (f) fclose(f);
    return 0;
}
