/* forced into every Munt source file (see the makefile): the library must never print, because it runs
 * inside SBEMU's sound interrupt. <cstdio> comes first so its own #undefs do not remove these. */
#include <cstdio>
#include <cstdarg>
extern "C" int mt32emu_silent_printf(const char*, ...);
extern "C" int mt32emu_silent_vprintf(const char*, va_list);
#define printf mt32emu_silent_printf
#define vprintf mt32emu_silent_vprintf
