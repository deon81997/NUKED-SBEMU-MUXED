/* measw.c - MEASW.EXE: runs a measurement script in the Windows 9x test VM without a console window
 * (a console window is a DOS box to SC55W: it would count as "a DOS game has the keyboard").
 * MEASW script.txt - lines:  run CMD (and wait) | start CMD | sleep MS | ini FILE SECTION KEY VALUE |
 * copy FROM TO | done FILE (writes "done") ; '#' = comment. GUI subsystem: -mwindows. */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static void run(char *cmd, int wait)
{
    STARTUPINFOA si; PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_SHOWMINNOACTIVE;
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return;
    if (wait) WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
}

int WINAPI WinMain(HINSTANCE i, HINSTANCE p, LPSTR cl, int sh)
{
    char line[512];
    FILE *f = fopen(cl && *cl ? cl : "D:\\MEAS.TXT", "r");
    (void)i; (void)p; (void)sh;
    if (!f) return 1;
    while (fgets(line, sizeof line, f)) {
        char *s = line, a[200], b[200], c[200], d[200];
        line[strcspn(line, "\r\n")] = 0;
        if (!*s || *s == '#') continue;
        if (!strncmp(s, "run ", 4)) run(s + 4, 1);
        else if (!strncmp(s, "start ", 6)) run(s + 6, 0);
        else if (!strncmp(s, "sleep ", 6)) Sleep(atoi(s + 6));
        else if (sscanf(s, "ini %199s %199s %199s %199s", a, b, c, d) == 4) WritePrivateProfileStringA(b, c, d, a);
        else if (sscanf(s, "copy %199s %199s", a, b) == 2) CopyFileA(a, b, FALSE);
        else if (sscanf(s, "done %199s", a) == 1) { FILE *o = fopen(a, "w"); if (o) { fputs("done\n", o); fclose(o); } }
    }
    fclose(f);
    return 0;
}
