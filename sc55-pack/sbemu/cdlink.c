/* CDLINK - see cdlink.h. GPL-2.0-or-later. */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "cdlink.h"

#define RING_BYTES (CDLINK_RING_SECTORS * CDLINK_SECTOR)

struct CDLINK_Engine
{
    CDLINK_IO io;
    int gain;               /* percent */
    uint8_t* ring;
    uint32_t rpos, wpos, avail;   /* bytes; rpos/wpos are frame aligned */
    uint32_t fill_sector;
    uint32_t start, end;
    uint32_t consumed;      /* frames played since start */
    uint32_t frac;          /* 16 bit fraction of the source position */
    uint16_t seen_gen;
    int seen_init;
    int session;            /* a play request is active (even if paused) */
    int paused;
    int opened;
    int pending_open;
    int open_tries;
    int fill_done;
    int primed;
};

CDLINK_Engine* CDLINK_EngineCreate(const CDLINK_IO* io, int gain_percent)
{
    CDLINK_Engine* e = (CDLINK_Engine*)calloc(1, sizeof *e);
    if (!e) return NULL;
    e->ring = (uint8_t*)malloc(RING_BYTES);
    if (!e->ring) { free(e); return NULL; }
    e->io = *io;
    e->gain = gain_percent;
    return e;
}

void CDLINK_EngineDestroy(CDLINK_Engine* e)
{
    if (!e) return;
    if (e->opened && e->io.close) e->io.close(e->io.user);
    free(e->ring);
    free(e);
}

static void stop_session(CDLINK_Engine* e)
{
    if (e->opened && e->io.close) e->io.close(e->io.user);
    e->opened = 0;
    e->pending_open = 0;
    e->session = 0;
    e->paused = 0;
    e->avail = e->rpos = e->wpos = 0;
    e->fill_done = 0;
    e->primed = 0;
}

static void begin_session(CDLINK_Engine* e, const CDLINK_Box* box)
{
    stop_session(e);
    e->start = box->start;
    e->end = box->end;
    e->fill_sector = box->start;
    e->consumed = 0;
    e->frac = 0;
    e->session = 1;
    e->pending_open = 1;
    e->open_tries = 0;
    if (e->end <= e->start) e->fill_done = 1;
}

static void refill(CDLINK_Engine* e)
{
    int done = 0;
    while (done < CDLINK_REFILL_SECTORS && !e->fill_done)
    {
        uint32_t free_bytes = RING_BYTES - e->avail;
        uint32_t room_sectors = free_bytes / CDLINK_SECTOR;
        uint32_t contiguous = (RING_BYTES - e->wpos) / CDLINK_SECTOR;
        uint32_t chunk = CDLINK_REFILL_SECTORS - done;
        int r;
        if (e->fill_sector >= e->end) { e->fill_done = 1; break; }
        if (chunk > room_sectors) chunk = room_sectors;
        if (chunk > contiguous) chunk = contiguous;
        if (chunk > e->end - e->fill_sector) chunk = e->end - e->fill_sector;
        if (chunk == 0) break;      /* ring full */
        r = e->io.read(e->io.user, e->fill_sector, e->ring + e->wpos, (int)chunk);
        if (r < 0) break;           /* DOS busy: next tick */
        if (r == 0) { e->fill_done = 1; break; }
        e->fill_sector += (uint32_t)r;
        e->wpos = (e->wpos + (uint32_t)r * CDLINK_SECTOR) % RING_BYTES;
        e->avail += (uint32_t)r * CDLINK_SECTOR;
        done += r;
        if (e->fill_sector >= e->end) e->fill_done = 1;
    }
}

static inline int sat16(int v) { return v > 32767 ? 32767 : (v < -32768 ? -32768 : v); }

int CDLINK_Mix(CDLINK_Engine* e, CDLINK_Box* box, int16_t* pcm, int frames, int rate)
{
    int i, mixed = 0, gl, gr;
    uint32_t step;

    if (!e->seen_init) { e->seen_init = 1; e->seen_gen = (uint16_t)(box->gen - (box->state != 0)); } /* a play that is already on is a new play */
    if (box->gen != e->seen_gen)
    {
        e->seen_gen = box->gen;
        if (box->state == 0)
            stop_session(e);
        else
        {
            if (box->cmd == 1 || !e->session)
                begin_session(e, box);
            e->paused = (box->state == 2);
        }
    }
    if (e->session && box->state == 0)      /* ended by someone else */
        stop_session(e);
    if (!e->session || e->paused || rate <= 0) return 0;

    if (e->pending_open)
    {
        int r = e->io.open(e->io.user, box);
        if (r == 0) { e->opened = 1; e->pending_open = 0; }
        else if (r > 0 || ++e->open_tries > 600)
        {
            stop_session(e);
            box->state = 0;
            return 0;
        }
        else return 0;
    }

    refill(e);
    if (!e->primed)
    {
        if (e->avail >= CDLINK_PREBUFFER_SECTORS * CDLINK_SECTOR || e->fill_done) e->primed = 1;
        else return 0;
    }

    step = (uint32_t)((((uint64_t)44100) << 16) / (uint64_t)rate);
    gl = (box->vol[0] * e->gain * 256) / (255 * 100);
    gr = (box->vol[1] * e->gain * 256) / (255 * 100);
    if (gl > 8192) gl = 8192;
    if (gr > 8192) gr = 8192;

    for (i = 0; i < frames; i++)
    {
        uint32_t frames_avail = e->avail / 4;
        const int16_t* s0;
        const int16_t* s1;
        int l, r;
        uint32_t adv;
        if (frames_avail == 0) break;
        if (frames_avail < 2 && !e->fill_done) break;   /* underrun */
        s0 = (const int16_t*)(e->ring + e->rpos);
        s1 = (frames_avail >= 2) ? (const int16_t*)(e->ring + ((e->rpos + 4) % RING_BYTES)) : s0;
        l = s0[0] + (int)(((int64_t)(s1[0] - s0[0]) * (int)(e->frac >> 1)) >> 15);
        r = s0[1] + (int)(((int64_t)(s1[1] - s0[1]) * (int)(e->frac >> 1)) >> 15);
        pcm[i * 2]     = (int16_t)sat16(pcm[i * 2]     + ((l * gl) >> 8));
        pcm[i * 2 + 1] = (int16_t)sat16(pcm[i * 2 + 1] + ((r * gr) >> 8));
        mixed = 1;
        e->frac += step;
        adv = e->frac >> 16;
        e->frac &= 0xFFFF;
        if (adv > frames_avail) adv = frames_avail;
        e->rpos = (e->rpos + adv * 4) % RING_BYTES;
        e->avail -= adv * 4;
        e->consumed += adv;
    }

    {
        uint32_t p = e->start + e->consumed / CDLINK_FRAMES_PER_SECTOR;
        if ((e->fill_done && e->avail < 4) || p >= e->end)
        {
            box->pos = e->end;
            box->state = 0;
            stop_session(e);
        }
        else
            box->pos = p;
    }
    return mixed;
}

/* ---- pure helpers ---------------------------------------------------------------------- */

static int ieq(const char* a, const char* b, int n)
{
    int i;
    for (i = 0; i < n; i++)
    {
        char x = a[i], y = b[i];
        if (x >= 'a' && x <= 'z') x -= 32;
        if (y >= 'a' && y <= 'z') y -= 32;
        if (x != y) return 0;
    }
    return 1;
}

int CDLINK_ParseCueFile(const char* text, int len, char* out, int outsz)
{
    int i = 0;
    while (i < len)
    {
        int ls = i, n = 0;
        while (i < len && text[i] != '\n' && text[i] != '\r') i++;
        /* line is text[ls..i) */
        while (ls < i && (text[ls] == ' ' || text[ls] == '\t')) ls++;
        if (i - ls > 5 && ieq(text + ls, "FILE", 4) && (text[ls + 4] == ' ' || text[ls + 4] == '\t'))
        {
            int p = ls + 5;
            while (p < i && (text[p] == ' ' || text[p] == '\t')) p++;
            if (p < i && text[p] == '"')
            {
                p++;
                while (p < i && text[p] != '"' && n < outsz - 1) out[n++] = text[p++];
            }
            else
                while (p < i && text[p] != ' ' && text[p] != '\t' && n < outsz - 1) out[n++] = text[p++];
            out[n] = 0;
            return n > 0;
        }
        while (i < len && (text[i] == '\n' || text[i] == '\r')) i++;
    }
    return 0;
}

void CDLINK_JoinPath(const char* cuepath, const char* name, char* out, int outsz)
{
    int last = -1, i, n = 0;
    if (name[0] == '\\' || (name[0] && name[1] == ':'))   /* already absolute */
        last = -2;
    else
        for (i = 0; cuepath[i]; i++) if (cuepath[i] == '\\' || cuepath[i] == '/' || cuepath[i] == ':') last = i;
    if (last >= 0)
        for (i = 0; i <= last && n < outsz - 1; i++) out[n++] = cuepath[i];
    for (i = 0; name[i] && n < outsz - 1; i++) out[n++] = name[i] == '/' ? '\\' : name[i];
    out[n] = 0;
}

#ifndef CDLINK_HOSTTEST
/* ---- DOS glue ---------------------------------------------------------------------------- */
#include "dpmi/dpmi.h"

#define DOSBUF_SECTORS 4
#define DOSBUF_BYTES (DOSBUF_SECTORS * CDLINK_SECTOR)

static CDLINK_Engine* g_eng;
static uint32_t g_box_lin;      /* linear address of the mailbox */
static uint32_t g_seg;          /* driver segment */
static uint32_t g_indos_lin;
static uint32_t g_dosbuf_lin;
static uint16_t g_dosbuf_seg;
static uint16_t g_own_psp;
static uint16_t g_handle = 0xFFFF;
static int g_pos_valid;
static int g_ready;

static int dos_safe(void)
{
    return DPMI_LoadB(g_indos_lin) == 0 && DPMI_LoadB(g_indos_lin - 1) == 0;
}

static uint16_t dos_get_psp(void)
{
    DPMI_REG r = {0};
    r.h.ah = 0x51;
    DPMI_CallRealModeINT(0x21, &r);
    return r.w.bx;
}

static void dos_set_psp(uint16_t psp)
{
    DPMI_REG r = {0};
    r.h.ah = 0x50;
    r.w.bx = psp;
    DPMI_CallRealModeINT(0x21, &r);
}

/* DOS call with our own PSP so file handles belong to SBEMU. */
static uint16_t dos_int21(DPMI_REG* r)
{
    uint16_t old = dos_get_psp();
    uint16_t rc;
    dos_set_psp(g_own_psp);
    rc = DPMI_CallRealModeINT(0x21, r);
    dos_set_psp(old);
    return rc;
}

static int dos_open_path(const char* path)
{
    DPMI_REG r = {0};
    int n = (int)strlen(path);
    if (n > 120) return -1;
    DPMI_LMemcpy(g_dosbuf_lin, DPMI_PTR2L((void*)path), (uint32_t)n + 1);
    r.h.ah = 0x3D;
    r.h.al = 0x40;
    r.w.ds = g_dosbuf_seg;
    r.w.dx = 0;
    if (dos_int21(&r) != 0 || (r.w.flags & 1)) return -1;
    return r.w.ax;
}

static void dos_close_handle(uint16_t h)
{
    DPMI_REG r = {0};
    r.h.ah = 0x3E;
    r.w.bx = h;
    dos_int21(&r);
}

static int dos_seek_read(uint16_t h, uint32_t byteoff, uint32_t dstlin, int bytes)
{
    DPMI_REG r = {0};
    r.h.ah = 0x42;
    r.h.al = 0;
    r.w.bx = h;
    r.w.cx = (uint16_t)(byteoff >> 16);
    r.w.dx = (uint16_t)byteoff;
    if (dos_int21(&r) != 0 || (r.w.flags & 1)) return -1;
    memset(&r, 0, sizeof r);
    r.h.ah = 0x3F;
    r.w.bx = h;
    r.w.cx = (uint16_t)bytes;
    r.w.ds = g_dosbuf_seg;
    r.w.dx = 0;
    if (dos_int21(&r) != 0 || (r.w.flags & 1)) return -1;
    if (r.w.ax) DPMI_LMemcpy(dstlin, g_dosbuf_lin, r.w.ax);
    return r.w.ax;
}

static int io_open(void* user, const CDLINK_Box* box)
{
    char path[130], bin[260], text[1100];
    int n, h, got;
    (void)user;
    if (!dos_safe()) return -1;
    DPMI_LMemcpy(DPMI_PTR2L(path), g_seg * 16 + box->unitp + 26, 128);
    path[128] = 0;
    n = (int)strlen(path);
    if (n == 0) return 1;
    if (n > 4 && ieq(path + n - 4, ".CUE", 4))
    {
        char name[130];
        h = dos_open_path(path);
        if (h < 0) return 1;
        {   /* read the first KB of the sheet through the DOS buffer */
            DPMI_REG r = {0};
            r.h.ah = 0x3F; r.w.bx = (uint16_t)h; r.w.cx = 1024; r.w.ds = g_dosbuf_seg; r.w.dx = 0;
            if (dos_int21(&r) != 0 || (r.w.flags & 1)) { dos_close_handle((uint16_t)h); return 1; }
            got = r.w.ax;
        }
        if (got > 0) DPMI_LMemcpy(DPMI_PTR2L(text), g_dosbuf_lin, (uint32_t)got);
        dos_close_handle((uint16_t)h);
        text[got > 0 ? got : 0] = 0;
        if (!CDLINK_ParseCueFile(text, got, name, sizeof name)) return 1;
        CDLINK_JoinPath(path, name, bin, sizeof bin);
    }
    else
        strcpy(bin, path);
    h = dos_open_path(bin);
    if (h < 0) return 1;
    g_handle = (uint16_t)h;
    g_pos_valid = 0;
    return 0;
}

static void io_close(void* user)
{
    (void)user;
    if (g_handle != 0xFFFF)
    {
        if (dos_safe()) dos_close_handle(g_handle);
        g_handle = 0xFFFF;
    }
}

static int io_read(void* user, uint32_t sector, uint8_t* dst, int n)
{
    int total = 0;
    (void)user;
    if (g_handle == 0xFFFF) return 0;
    if (!dos_safe()) return -1;
    while (n > 0)
    {
        int c = n > DOSBUF_SECTORS ? DOSBUF_SECTORS : n;
        int got = dos_seek_read(g_handle, sector * CDLINK_SECTOR, DPMI_PTR2L(dst), c * CDLINK_SECTOR);
        if (got < 0) return total ? total : 0;
        got /= CDLINK_SECTOR;
        if (got == 0) break;
        total += got; n -= got; sector += (uint32_t)got; dst += got * CDLINK_SECTOR;
        if (got < c) break;
    }
    return total;
}

int CDLINK_Init(int gain_percent)
{
    DPMI_REG r = {0};
    uint32_t lol, dev, seg_dos;
    uint8_t* scan;
    int found = 0, hops;
    CDLINK_IO io;

    if (gain_percent <= 0) return 0;
    r.h.ah = 0x52;                              /* list of lists */
    if (DPMI_CallRealModeINT(0x21, &r) != 0) return 0;
    lol = ((uint32_t)r.w.es << 4) + r.w.bx;
    dev = lol + 0x22;                           /* NUL device header */
    g_seg = 0;
    for (hops = 0; hops < 64; hops++)
    {
        char name[9];
        uint16_t off, seg;
        off = DPMI_LoadW(dev);
        seg = DPMI_LoadW(dev + 2);
        DPMI_LMemcpy(DPMI_PTR2L(name), dev + 10, 8);
        name[8] = 0;
        if (!strncmp(name, "UCDD0001", 8))
        {
            if (dev & 0xF) return 0;           /* the driver's offsets assume its header is at offset 0 */
            g_seg = dev >> 4;
            found = 1;
            break;
        }
        if (off == 0xFFFF) break;
        dev = ((uint32_t)seg << 4) + off;
    }
    if (!found) return 0;

    /* the driver segment starts at its device header; the mailbox is inside the first 60 KB */
    scan = (uint8_t*)malloc(0xF000);
    if (!scan) return 0;
    DPMI_LMemcpy(DPMI_PTR2L(scan), g_seg * 16, 0xF000);
    found = 0;
    {
        uint32_t i;
        for (i = 0; i + sizeof(CDLINK_Box) < 0xF000; i++)
            if (!memcmp(scan + i, "SBCDA1\0\0", 8) && scan[i + 30] == 1 && scan[i + 31] == 0)
            { g_box_lin = g_seg * 16 + i; found = 1; break; }
    }
    free(scan);
    if (!found) return 0;

    memset(&r, 0, sizeof r);
    r.h.ah = 0x34;                              /* InDOS flag */
    if (DPMI_CallRealModeINT(0x21, &r) != 0) return 0;
    g_indos_lin = ((uint32_t)r.w.es << 4) + r.w.bx;

    seg_dos = DPMI_DOSMalloc((DOSBUF_BYTES + 15) / 16);
    if (!seg_dos) return 0;
    g_dosbuf_seg = (uint16_t)(seg_dos & 0xFFFF);
    g_dosbuf_lin = (uint32_t)g_dosbuf_seg << 4;
    g_own_psp = dos_get_psp();

    io.open = io_open; io.close = io_close; io.read = io_read; io.user = NULL;
    g_eng = CDLINK_EngineCreate(&io, gain_percent);
    if (!g_eng) return 0;
    DPMI_StoreB(g_box_lin + CDLINK_BOX_FLAGS, (uint8_t)(DPMI_LoadB(g_box_lin + CDLINK_BOX_FLAGS) | 1));
    g_ready = 1;
    return 1;
}

int CDLINK_IsActive(void) { return g_ready; }

void CDLINK_MixInto(int16_t* pcm, int frames, int rate)
{
    CDLINK_Box box, before;
    if (!g_ready) return;
    DPMI_LMemcpy(DPMI_PTR2L(&box), g_box_lin, sizeof box);
    before = box;
    CDLINK_Mix(g_eng, &box, pcm, frames, rate);
    if (box.pos != before.pos) DPMI_StoreD(g_box_lin + CDLINK_BOX_POS, box.pos);
    if (box.state != before.state && DPMI_LoadW(g_box_lin + 24) == before.gen)
        DPMI_StoreB(g_box_lin + CDLINK_BOX_STATE, box.state);
}

void CDLINK_Shutdown(void)
{
    if (!g_ready) return;
    g_ready = 0;
    DPMI_StoreB(g_box_lin + CDLINK_BOX_FLAGS, (uint8_t)(DPMI_LoadB(g_box_lin + CDLINK_BOX_FLAGS) & ~1));
    DPMI_StoreB(g_box_lin + CDLINK_BOX_STATE, 0);
    CDLINK_EngineDestroy(g_eng);
    g_eng = NULL;
}
#endif
