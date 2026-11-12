/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* vxdtest.c - load a dynamic VxD, call it, write the result to a log. */
#include <windows.h>
#include <stdio.h>
int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "\\\\.\\D:\\HELLO.VXD";
    FILE *log = fopen("C:\\VXDTEST.LOG", "a");
    HANDLE h = CreateFileA(path, 0, 0, NULL, 0, FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        fprintf(log, "open %s failed: %lu\n", path, GetLastError());
    } else {
        DWORD out[2] = {0, 0}, got = 0;
        BOOL ok = DeviceIoControl(h, 1, NULL, 0, out, sizeof out, &got, NULL);
        fprintf(log, "%s: open ok; ioctl=%d got=%lu calls=%lu tag=%.4s\n", path, ok, got, out[0], (char *)&out[1]);
        ok = DeviceIoControl(h, 1, NULL, 0, out, sizeof out, &got, NULL);
        fprintf(log, "second call: calls=%lu\n", out[0]);
        CloseHandle(h);
    }
    fclose(log);
    return 0;
}
