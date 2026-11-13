/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU */
/* fakebios.h - the fake DOS memory, VESA BIOS, frame buffer and mouse for the host sanitizer tests (m3asan.c: MIDPLAY,
 * m3menu.c: the 3D menu). Included once, after the program under test. */
unsigned char hs_dos[0x110000];
HS_MODE hs_modes[64]; int hs_nmodes, hs_fail_set;
unsigned char* hs_lfb; unsigned long hs_lfb_size; int hs_cur_mode = 3;
static int hs_bank, hs_pitch;
static unsigned long hs_frames;

void hs_check(unsigned long a, unsigned long n, const char* what)
{
    if (a + n > sizeof hs_dos) { fprintf(stderr, "FAIL: %s outside DOS memory: %lx+%lu\n", what, a, n); abort(); }
    if (a >= 0xA0000 && a < 0xB0000 && hs_cur_mode != 0x13 && hs_cur_mode != 3)
    {   /* a write through the 64K bank window: must stay inside the visible screen */
        unsigned long lin = (unsigned long)hs_bank * 65536 + (a - 0xA0000);
        if (a + n > 0xB0000 || lin + n > hs_lfb_size) { fprintf(stderr, "FAIL: %s bank %d off %lx+%lu past the screen (%lu)\n", what, hs_bank, a - 0xA0000, n, hs_lfb_size); abort(); }
    }
    if (hs_cur_mode == 0x13 && a >= 0xA0000 && a < 0xB0000 && a + n > 0xA0000 + 64000) { fprintf(stderr, "FAIL: %s past mode 13h screen\n", what); abort(); }
}
unsigned long hs_ticks(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (unsigned long)((t.tv_sec * 1000000000LL + t.tv_nsec) / 54925400LL); }
uclock_t uclock(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uclock_t)((t.tv_sec * 1000000000LL + t.tv_nsec) / 838LL); }
void movedata(unsigned ss, unsigned so, unsigned ds, unsigned dofs, size_t n)
{
    if (ss != 2 || ds != 3) { fprintf(stderr, "FAIL: movedata %u -> %u\n", ss, ds); abort(); }
    if (!hs_lfb || dofs + n > hs_lfb_size) { fprintf(stderr, "FAIL: LFB write %u+%zu past the screen (%lu)\n", dofs, n, hs_lfb_size); abort(); }
    memcpy(hs_lfb + dofs, (const void*)(uintptr_t)so, n);
}
static HS_MODE* hs_find(int mode) { int i; for (i = 0; i < hs_nmodes; i++) if (hs_modes[i].mode == (mode & 0x1FF)) return &hs_modes[i]; return NULL; }
static int hs_pitch_of(const HS_MODE* m) { return m->w * ((m->bpp + 7) / 8); }
/* a fake mouse driver (INT 33h), only when a test turns it on: position, buttons, wheel set by the test */
int hs_mouse_on, hs_mx, hs_my, hs_mb, hs_mwheel, hs_mxmax = 639, hs_mymax = 199;
int __dpmi_int(int v, __dpmi_regs* r)
{
    unsigned long es = (unsigned long)r->x.es * 16 + r->x.di;
    if (v == 0x33 && hs_mouse_on)
    {
        switch (r->x.ax)
        {
        case 0: r->x.ax = 0xFFFF; r->x.bx = 2; break;
        case 3:
            if (hs_mx > hs_mxmax) hs_mx = hs_mxmax; if (hs_my > hs_mymax) hs_my = hs_mymax;
            r->x.bx = (unsigned short)(hs_mb | (hs_mwheel & 255) << 8); r->x.cx = (unsigned short)hs_mx; r->x.dx = (unsigned short)hs_my; hs_mwheel = 0; break;
        case 4: hs_mx = r->x.cx; hs_my = r->x.dx; break;
        case 7: hs_mxmax = r->x.dx; break;
        case 8: hs_mymax = r->x.dx; break;
        default: break;
        }
        return 0;
    }
    if (v != 0x10) return 0;
    if (r->h.ah == 0x00)
    {
        hs_cur_mode = r->h.al & 0x7F; hs_dos[0x449] = (unsigned char)hs_cur_mode;
        free(hs_lfb); hs_lfb = NULL; hs_lfb_size = hs_cur_mode == 0x13 ? 64000 : 0;
        return 0;
    }
    switch (r->x.ax)
    {
    case 0x4F00:
    {
        unsigned char* vi = hs_dos + es; int i;
        hs_check(es, 512, "4F00");
        memset(vi, 0, 512); memcpy(vi, "VESA", 4); vi[4] = 0; vi[5] = 3;
        vi[0x0E] = 0; vi[0x0F] = 0; vi[0x10] = 0x00; vi[0x11] = 0x80;          /* mode list at 8000:0000 */
        for (i = 0; i < hs_nmodes; i++) { hs_dos[0x80000 + i * 2] = hs_modes[i].mode & 255; hs_dos[0x80001 + i * 2] = (unsigned char)(hs_modes[i].mode >> 8); }
        hs_dos[0x80000 + i * 2] = 0xFF; hs_dos[0x80001 + i * 2] = 0xFF;
        r->x.ax = 0x004F; return 0;
    }
    case 0x4F01:
    {
        HS_MODE* m = hs_find(r->x.cx); unsigned char* mi = hs_dos + es; int p;
        if (!m) { r->x.ax = 0x014F; return 0; }
        hs_check(es, 256, "4F01");
        memset(mi, 0, 256); p = hs_pitch_of(m);
        mi[0] = 0x9B; mi[2] = 7; mi[4] = 64; mi[6] = 64; mi[8] = 0x00; mi[9] = 0xA0;
        mi[0x10] = p & 255; mi[0x11] = (unsigned char)(p >> 8);
        mi[0x12] = m->w & 255; mi[0x13] = (unsigned char)(m->w >> 8); mi[0x14] = m->h & 255; mi[0x15] = (unsigned char)(m->h >> 8);
        mi[0x19] = (unsigned char)m->bpp; mi[0x1B] = m->bpp == 8 ? 4 : 6;
        if (m->bpp == 15) { mi[0x1F] = 5; mi[0x20] = 10; mi[0x21] = 5; mi[0x22] = 5; mi[0x23] = 5; mi[0x24] = 0; }
        else if (m->bpp == 16) { mi[0x1F] = 5; mi[0x20] = 11; mi[0x21] = 6; mi[0x22] = 5; mi[0x23] = 5; mi[0x24] = 0; }
        else if (m->bpp >= 24) { mi[0x1F] = 8; mi[0x20] = 16; mi[0x21] = 8; mi[0x22] = 8; mi[0x23] = 8; mi[0x24] = 0; }
        mi[0x2B] = 0xE0; mi[0x32] = p & 255; mi[0x33] = (unsigned char)(p >> 8);
        r->x.ax = 0x004F; return 0;
    }
    case 0x4F02:
    {
        HS_MODE* m = hs_find(r->x.bx);
        if (!m || hs_fail_set) { r->x.ax = 0x014F; return 0; }
        hs_cur_mode = m->mode; hs_pitch = hs_pitch_of(m); hs_bank = 0;
        free(hs_lfb); hs_lfb_size = (unsigned long)hs_pitch * m->h; hs_lfb = (unsigned char*)malloc(hs_lfb_size);
        r->x.ax = 0x004F; return 0;
    }
    case 0x4F05: hs_bank = r->x.dx; r->x.ax = 0x004F; return 0;
    case 0x4F09: case 0x4F0B: r->x.ax = 0x004F; return 0;
    default: return 0;
    }
}

static void set_modes(void)
{
    static const int sz[][2] = { {320,200}, {320,240}, {321,201}, {400,300}, {512,384}, {640,350}, {640,400}, {640,480}, {720,480},
        {800,600}, {848,480}, {1024,600}, {1024,768}, {1152,864}, {1280,720}, {1280,800}, {1280,960}, {1280,1024}, {1360,768},
        {1366,768}, {1400,1050}, {1440,900}, {1599,1199}, {1600,900}, {1600,1200}, {1680,1050}, {1920,1080}, {2,2} };
    static const int bp[] = { 8, 15, 16, 24, 32 };
    int i, b; hs_nmodes = 0;
    for (i = 0; sz[i][0] > 2; i++) for (b = 0; b < 5; b++)
    {
        if (hs_nmodes >= 64) break;
        int full = (sz[i][0] == 320 && sz[i][1] == 200) || (sz[i][0] == 640 && sz[i][1] == 480) || (sz[i][0] == 800 && sz[i][1] == 600) || (sz[i][0] == 1600 && sz[i][1] == 1200);
        if (!full && b != i % 5) continue;                                   /* the others in one depth each (list < 64) */
        hs_modes[hs_nmodes].w = sz[i][0]; hs_modes[hs_nmodes].h = sz[i][1]; hs_modes[hs_nmodes].bpp = bp[b];
        hs_modes[hs_nmodes].mode = 0x100 + hs_nmodes; hs_nmodes++;
    }
}
