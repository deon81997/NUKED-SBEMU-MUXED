/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* compat98.c - replacements for Windows Vista+ KERNEL32 functions that
 * MinGW-w64's libstdc++/libgcc reference. Defining the __imp_ pointers here
 * makes the linker use these instead of importing from KERNEL32, so the EXE
 * loads on Windows 98. Each one calls the real function when it exists
 * (newer Windows) and otherwise uses a simple fallback. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static FARPROC k32(const char *name)
{
    HMODULE h = GetModuleHandleA("KERNEL32.DLL");
    return h ? GetProcAddress(h, name) : NULL;
}

/* Condition variables: fallback is a generation counter polled with Sleep.
 * Correct (no lost wakeups beyond spurious ones, which callers must handle)
 * but slow; nothing in this project waits on condition variables. */
typedef struct { volatile LONG gen; } CV98;

static void WINAPI my_InitializeConditionVariable(PCONDITION_VARIABLE cv)
{
    static FARPROC f; static int t;
    if (!t) { f = k32("InitializeConditionVariable"); t = 1; }
    if (f) { ((void (WINAPI *)(PCONDITION_VARIABLE))f)(cv); return; }
    ((CV98 *)cv)->gen = 0;
}
static void WINAPI my_WakeConditionVariable(PCONDITION_VARIABLE cv)
{
    static FARPROC f; static int t;
    if (!t) { f = k32("WakeConditionVariable"); t = 1; }
    if (f) { ((void (WINAPI *)(PCONDITION_VARIABLE))f)(cv); return; }
    InterlockedIncrement(&((CV98 *)cv)->gen);
}
static void WINAPI my_WakeAllConditionVariable(PCONDITION_VARIABLE cv)
{
    static FARPROC f; static int t;
    if (!t) { f = k32("WakeAllConditionVariable"); t = 1; }
    if (f) { ((void (WINAPI *)(PCONDITION_VARIABLE))f)(cv); return; }
    InterlockedIncrement(&((CV98 *)cv)->gen);
}
static BOOL WINAPI my_SleepConditionVariableCS(PCONDITION_VARIABLE cv, PCRITICAL_SECTION cs, DWORD ms)
{
    static FARPROC f; static int t;
    if (!t) { f = k32("SleepConditionVariableCS"); t = 1; }
    if (f) return ((BOOL (WINAPI *)(PCONDITION_VARIABLE, PCRITICAL_SECTION, DWORD))f)(cv, cs, ms);
    {
        LONG start = ((CV98 *)cv)->gen;
        DWORD t0 = GetTickCount();
        LeaveCriticalSection(cs);
        while (((CV98 *)cv)->gen == start) {
            if (ms != INFINITE && GetTickCount() - t0 >= ms) {
                EnterCriticalSection(cs);
                SetLastError(ERROR_TIMEOUT);
                return FALSE;
            }
            Sleep(1);
        }
        EnterCriticalSection(cs);
        return TRUE;
    }
}
static DWORD WINAPI my_GetThreadId(HANDLE th)
{
    static FARPROC f; static int t;
    if (!t) { f = k32("GetThreadId"); t = 1; }
    if (f) return ((DWORD (WINAPI *)(HANDLE))f)(th);
    if (th == GetCurrentThread()) return GetCurrentThreadId();
    return 0;   /* unknown on Windows 98 */
}

void *__imp__InitializeConditionVariable_4 __asm__("__imp__InitializeConditionVariable@4") = (void *)my_InitializeConditionVariable;
void *__imp__WakeConditionVariable_4       __asm__("__imp__WakeConditionVariable@4")       = (void *)my_WakeConditionVariable;
void *__imp__WakeAllConditionVariable_4    __asm__("__imp__WakeAllConditionVariable@4")    = (void *)my_WakeAllConditionVariable;
void *__imp__SleepConditionVariableCS_12   __asm__("__imp__SleepConditionVariableCS@12")   = (void *)my_SleepConditionVariableCS;
void *__imp__GetThreadId_4                 __asm__("__imp__GetThreadId@4")                 = (void *)my_GetThreadId;

/* ---- used by libstdc++ (thread_local destructors): keep-module-loaded --- */
static BOOL WINAPI my_GetModuleHandleExW(DWORD flags, LPCWSTR name, HMODULE *mod)
{
    static FARPROC f; static int t;
    if (!t) { f = k32("GetModuleHandleExW"); t = 1; }
    if (f) return ((BOOL (WINAPI *)(DWORD, LPCWSTR, HMODULE *))f)(flags, name, mod);
    /* everything is linked statically into the EXE: the module is the EXE */
    if (mod) *mod = GetModuleHandleA(NULL);
    return TRUE;
}
void *__imp__GetModuleHandleExW_12 __asm__("__imp__GetModuleHandleExW@12") = (void *)my_GetModuleHandleExW;

/* ---- MSVCRT functions newer than Windows 98's MSVCRT.DLL --------------- */
#include <sys/stat.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>

static int __cdecl my_fstat64(int fd, struct _stat64 *st)
{
    struct _stati64 s;
    int r = _fstati64(fd, &s);
    if (r) return r;
    st->st_dev = s.st_dev; st->st_ino = s.st_ino; st->st_mode = s.st_mode;
    st->st_nlink = s.st_nlink; st->st_uid = s.st_uid; st->st_gid = s.st_gid;
    st->st_rdev = s.st_rdev; st->st_size = s.st_size;
    st->st_atime = s.st_atime; st->st_mtime = s.st_mtime; st->st_ctime = s.st_ctime;
    return 0;
}

static unsigned __int64 parse_u64(const char *s, char **end, int base, int *neg, int *overflow)
{
    const char *p = s;
    unsigned __int64 v = 0;
    int any = 0;
    *neg = 0; *overflow = 0;
    while (isspace((unsigned char)*p)) p++;
    if (*p == '-' || *p == '+') { *neg = *p == '-'; p++; }
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { p += 2; base = 16; }
    else if (base == 0) base = p[0] == '0' ? 8 : 10;
    for (;; p++) {
        int c = (unsigned char)*p, d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
        else break;
        if (d >= base) break;
        if (v > (~(unsigned __int64)0 - d) / base) *overflow = 1;
        v = v * base + d;
        any = 1;
    }
    if (end) *end = (char *)(any ? p : s);
    return v;
}
static __int64 __cdecl my_strtoi64(const char *s, char **end, int base)
{
    int neg, ovf;
    unsigned __int64 v = parse_u64(s, end, base, &neg, &ovf);
    if (ovf || v > (unsigned __int64)LLONG_MAX + neg) { errno = ERANGE; return neg ? LLONG_MIN : LLONG_MAX; }
    return neg ? -(__int64)v : (__int64)v;
}
static unsigned __int64 __cdecl my_strtoui64(const char *s, char **end, int base)
{
    int neg, ovf;
    unsigned __int64 v = parse_u64(s, end, base, &neg, &ovf);
    if (ovf) { errno = ERANGE; return ~(unsigned __int64)0; }
    return neg ? (unsigned __int64)(-(__int64)v) : v;
}
void *__imp___fstat64   __asm__("__imp___fstat64")   = (void *)my_fstat64;
void *__imp___strtoi64  __asm__("__imp___strtoi64")  = (void *)my_strtoi64;
void *__imp___strtoui64 __asm__("__imp___strtoui64") = (void *)my_strtoui64;
/* libmsvcrt maps strtoll/strtoull straight to MSVCRT's _strtoi64/_strtoui64 */
long long strtoll_98(const char *s, char **e, int b) __asm__("_strtoll");
unsigned long long strtoull_98(const char *s, char **e, int b) __asm__("_strtoull");
long long strtoll_98(const char *s, char **e, int b) { return my_strtoi64(s, e, b); }
unsigned long long strtoull_98(const char *s, char **e, int b) { return my_strtoui64(s, e, b); }
void *__imp__strtoll_98  __asm__("__imp__strtoll")  = (void *)strtoll_98;
void *__imp__strtoull_98 __asm__("__imp__strtoull") = (void *)strtoull_98;
