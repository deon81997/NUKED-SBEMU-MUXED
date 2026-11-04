/* sbtest.c - attach to SC55W.VXD and record what it produces.
 * Writes C:\SBTEST.LOG (status, events) and C:\SBTEST.WAV (the PCM ring).
 * Usage: SBTEST [seconds] [vxdpath]                                   */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "../common/sc55wif.h"

static void wav_header(FILE *f, unsigned rate, unsigned frames)
{
    unsigned bytes = frames * 4;
    unsigned hdr[11];
    memcpy(&hdr[0], "RIFF", 4); hdr[1] = 36 + bytes; memcpy(&hdr[2], "WAVE", 4);
    memcpy(&hdr[3], "fmt ", 4); hdr[4] = 16; hdr[5] = 1 | (2 << 16); hdr[6] = rate;
    hdr[7] = rate * 4; hdr[8] = 4 | (16 << 16); memcpy(&hdr[9], "data", 4); hdr[10] = bytes;
    fseek(f, 0, SEEK_SET); fwrite(hdr, 4, 11, f);
}

int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 60;
    const char *path = argc > 2 ? argv[2] : "\\\\.\\D:\\SC55W.VXD";
    FILE *log = fopen("C:\\SBTEST.LOG", "w");
    FILE *wav = fopen("C:\\SBTEST.WAV", "wb");
    SC55W_SHARED *s;
    HANDLE h;
    DWORD got = 0, st = 0, ver = 0, t0, vb[2] = {0, 0};
    unsigned frames = 0, rate = 0, nev = 0, nmidi = 0, nopl = 0, ndac = 0;

    setvbuf(log, NULL, _IONBF, 0);
    h = CreateFileA(path, 0, 0, NULL, 0, FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (h == INVALID_HANDLE_VALUE) { fprintf(log, "open failed %lu\n", GetLastError()); return 1; }
    DeviceIoControl(h, IOCTL_SC55W_VERSION, NULL, 0, vb, 8, &got, NULL);
    ver = vb[0];
    fprintf(log, "vxd interface version %lu (expect %d), build %lu\n", ver, SC55W_IF_VERSION, vb[1]);

    s = (SC55W_SHARED *)VirtualAlloc(NULL, sizeof *s, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    memset(s, 0, sizeof *s);
    s->magic = SC55W_MAGIC; s->if_version = SC55W_IF_VERSION; s->size = sizeof *s;
    s->cfg.sb_port = 0x220; s->cfg.mpu_port = 0x330; s->cfg.irq = 5;
    s->cfg.dma8 = 1; s->cfg.dma16 = 5; s->cfg.sbtype = 6; s->cfg.opl = 1;
    {
        DWORD addr = (DWORD)s;
        if (!DeviceIoControl(h, IOCTL_SC55W_ATTACH, &addr, 4, &st, 4, &got, NULL))
            fprintf(log, "attach ioctl failed %lu\n", GetLastError());
    }
    fprintf(log, "attach status %08lx (size %u)\n", st, (unsigned)sizeof *s);
    fwrite("RIFF", 1, 44, wav);   /* placeholder header */

    t0 = GetTickCount();
    while (GetTickCount() - t0 < (DWORD)secs * 1000) {
        while (s->fmt_tail != s->fmt_head) {
            SC55W_PCMFMT *f = &s->fmt[s->fmt_tail & (PCMFMT_SIZE - 1)];
            fprintf(log, "[%6lu ms] pcm rate %lu Hz from frame %lu\n", GetTickCount() - t0, f->rate, f->frame);
            if (!rate) rate = f->rate;
            s->fmt_tail++;
        }
        while (s->pcm_tail != s->pcm_head) {
            unsigned i = s->pcm_tail & (PCMRING_SIZE - 1);
            unsigned n = s->pcm_head - s->pcm_tail;
            if (n > PCMRING_SIZE - i) n = PCMRING_SIZE - i;
            fwrite(&s->pcm[i * 2], 4, n, wav);
            s->pcm_tail += n; frames += n;
        }
        while (s->ev_tail != s->ev_head) {
            SC55W_EVENT *e = &s->ev[s->ev_tail & (EVRING_SIZE - 1)];
            nev++;
            if (e->type == EV_MIDI) { nmidi++; if (nmidi <= 40) fprintf(log, "midi %02x t=%lu\n", e->a, e->time); }
            else if (e->type == EV_OPL) { nopl++; if (nopl <= 20) fprintf(log, "opl b%d %02x=%02x\n", e->c, e->a, e->b); }
            else if (e->type == EV_DAC) ndac++;
            else fprintf(log, "event type %d a=%d\n", e->type, e->a);
            s->ev_tail++;
        }
        Sleep(5);
    }
    fprintf(log, "frames %u, events %u (midi %u, opl %u, dac %u)\n", frames, nev, nmidi, nopl, ndac);
    fprintf(log, "irqs %lu dma_bytes %lu dsp_cmds %lu ev_ovf %lu pcm_ovf %lu last_vm %08lx\n",
            s->irq_count, s->dma_bytes, s->dsp_cmds, s->ev_overflow, s->pcm_overflow, s->last_vm);
    {
        int i;
        fprintf(log, "debug:");
        for (i = 0; i < 16; i++) fprintf(log, " %lx", s->debug[i]);
        fprintf(log, "\n");
    }
    {
        unsigned i, n = s->tr_head < TRACE_SIZE ? s->tr_head : TRACE_SIZE;
        unsigned st0 = s->tr_head < TRACE_SIZE ? 0 : s->tr_head;
        static const char *names[] = {"?","START","RATE","BLOCK","DMAGET","BURST","DMACB","CB","NOCHAN","MASKED","RESTOR","RBRANCH"};
        for (i = 0; i < n; i++) {
            u32 *t = s->tr[(st0 + i) & (TRACE_SIZE - 1)];
            fprintf(log, "trace %10lu %-6s %08lx %08lx\n", t[0], t[1] < 12 ? names[t[1]] : "?", t[2], t[3]);
        }
    }
    wav_header(wav, rate ? rate : 22050, frames);
    fclose(wav);
    DeviceIoControl(h, IOCTL_SC55W_DETACH, NULL, 0, NULL, 0, &got, NULL);
    CloseHandle(h);
    if (argc > 3) {
        char n[64];
        wsprintfA(n, "\\\\.\\%s", argv[3]);
        fprintf(log, "unload %s: %d\n", n, DeleteFileA(n));
    }
    fprintf(log, "done\n");
    fclose(log);
    return 0;
}
