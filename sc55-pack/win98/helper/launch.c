/* launch.c - SC55W.EXE: picks the build this CPU can run and starts it with the same command line.
 *   SC55WM.EXE  Pentium M build (SSE2: Pentium 4 / M, Celeron D / M, Athlon 64 and newer) - the fast one
 *   SC55W2.EXE  Pentium II build (no SSE2: Pentium II / III, Athlon XP, K6-2 and up with CMOV)
 * Built for a plain 386 so it runs anywhere; it waits for the build it started and returns its exit
 * code (SC55W.EXE /EXIT in a batch file still waits). GPL-2.0-or-later. */
#include <windows.h>
#include <string.h>

static int has_sse2(void)
{
    unsigned a, b, c, d, f0, f1;
    /* CPUID present? (EFLAGS.ID can be toggled) */
    __asm__ __volatile__("pushfl; popl %0; movl %0, %1; xorl $0x200000, %0; pushl %0; popfl; pushfl; popl %0; pushl %1; popfl"
                         : "=&r"(f0), "=&r"(f1));
    if (!((f0 ^ f1) & 0x200000)) return 0;
    __asm__ __volatile__("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1), "c"(0));
    return (d >> 26) & 1;
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show)
{
    char exe[MAX_PATH], line[MAX_PATH + 1024];
    char* slash;
    STARTUPINFOA si; PROCESS_INFORMATION pi; DWORD code = 1;
    (void)hi; (void)hp; (void)show;
    GetModuleFileNameA(NULL, exe, sizeof exe);
    slash = strrchr(exe, '\\');
    strcpy(slash ? slash + 1 : exe, has_sse2() ? "SC55WM.EXE" : "SC55W2.EXE");
    if (GetFileAttributesA(exe) == 0xFFFFFFFF && has_sse2()) strcpy(slash ? slash + 1 : exe, "SC55W2.EXE");
    lstrcpynA(line, "\"", sizeof line); lstrcatA(line, exe); lstrcatA(line, "\" "); lstrcatA(line, cmd ? cmd : "");
    memset(&si, 0, sizeof si); si.cb = sizeof si;
    if (!CreateProcessA(exe, line, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        MessageBoxA(NULL, "SC55WM.EXE / SC55W2.EXE is missing next to SC55W.EXE.\nCopy the whole FILES\\WIN98 folder again.",
                    "NUKED SB-EMU for Windows 98", MB_OK | MB_ICONSTOP);
        return 1;
    }
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return (int)code;
}
