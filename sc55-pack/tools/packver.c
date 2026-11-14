/* packver.c - one pack version for every program (linked into each EXE by the makefile).
   PROGRAM /VER prints "PROGRAM.EXE: NUKED SB-EMU <version>" and exits before main() runs,
   so it works the same in every tool, even ones that take over the screen or USB.
   The tag string lets anyone find the version in a binary: FIND "NUKED SB-EMU 1." FILE.EXE */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <crt0.h>

#ifndef PACK_VER
#define PACK_VER "dev"
#endif

const char pack_version[] = PACK_VER;
const char pack_version_tag[] __attribute__((used)) = "$VER: NUKED SB-EMU " PACK_VER " $";

__attribute__((constructor)) static void pack_ver_option(void)
{
    int i;
    for (i = 1; i < __crt0_argc; i++) {
        const char* a = __crt0_argv[i];
        if ((a[0] == '/' || a[0] == '-') && toupper((unsigned char)a[1]) == 'V' && toupper((unsigned char)a[2]) == 'E'
            && toupper((unsigned char)a[3]) == 'R' && a[4] == 0) {
            const char* p = __crt0_argv[0], *s;
            char name[16]; int n = 0;
            for (s = p; *s; s++) if (*s == '\\' || *s == '/' || *s == ':') p = s + 1;
            while (p[n] && n < 15) { name[n] = (char)toupper((unsigned char)p[n]); n++; }
            name[n] = 0;
            printf("%s: NUKED SB-EMU %s\n", name, PACK_VER);
            fflush(stdout);
            exit(0);
        }
    }
}
