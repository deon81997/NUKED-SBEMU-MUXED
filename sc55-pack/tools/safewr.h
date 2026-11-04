/* safewr.h - crash-safe file saving for the pack's tools.
 * sw_open() opens <name>.$$$ next to the file; sw_close() pushes it to the disk (fflush + DOS commit),
 * then swaps it in. If the PC hangs or loses power while saving, the old file is still there whole
 * (or, in the moment of the swap, the new one); a half-written settings file can no longer happen. */
#ifndef SAFEWR_INCLUDED
#define SAFEWR_INCLUDED
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static __attribute__((unused)) FILE* sw_open(const char* path, char* tmp, const char* mode)
{
    char* dot; char* sl;
    strcpy(tmp, path);
    dot = strrchr(tmp, '.'); sl = strrchr(tmp, '\\');
    if (dot && (!sl || dot > sl)) strcpy(dot, ".$$$"); else strcat(tmp, ".$$$");
    return fopen(tmp, mode);
}
static __attribute__((unused)) int sw_close(FILE* f, const char* tmp, const char* path)
{
    int ok = fflush(f) == 0;
    if (ok) fsync(fileno(f));
    if (fclose(f) != 0) ok = 0;
    if (!ok) { remove(tmp); return 0; }
    remove(path);
    if (rename(tmp, path) != 0) return 0;
    return 1;
}
#endif
