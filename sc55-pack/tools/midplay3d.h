/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* midplay3d.h - MIDPLAY's 3D view (S cycles: channel table -> SC-55 panel -> 3D). Included by midplay.c.
 *
 * 640x480 VESA, drawn by the CPU with the 3D menu's core (m3core.h): a NUKED SB-EMU module whose LCD shows the
 * synth's live display (part, instrument, the 16-part level dots) and whose knob turns with the song, a
 * keyboard in front of it that lights up the notes being played in each channel's colour, and 16 columns on
 * the mirror floor bouncing with the channel levels. Song, time and keys in a bar along the bottom.
 *
 * MIDPLAY sends the MIDI events from inside the drawing (m3_hook = mp_pump: every 8 scan lines), so frames never
 * make the music late. Frames follow each other back to back (at most 60 a second, the screen refresh): the old pause of 1.5 frame-times
 * between frames (drawing at most ~40% of the CPU) dates from before the music was sent from inside the drawing, and
 * left the D810 at ~5 frames a second. The floor is drawn row by row (m3_floor_rows), about 3x faster. Always full 640x480 (half resolution only with F4); a PC that
 * manages only ~2 frames a second gets a note. The other pages are text: the view
 * switches back to text mode for them and returns afterwards. */
#ifndef MIDPLAY3D_INCLUDED
#define MIDPLAY3D_INCLUDED
#include "m3core.h"

static int    mp3_on;                 /* the VESA mode is set */
static int    mp3_slow;               /* frames so slow that the picture barely moves (a note says so) */
static double mp3_last_frame, mp3_frame_ms;
static float  mp3_col[16];            /* smoothed column heights */
static M3SURF mp3_panel, mp3_keys, mp3_face;   /* mp3_face: the module's brushed face, made once */
static const uint32_t mp3_chcol0[16] = {                 /* the channels' colours: C picks a theme (mp3_theme_apply) */
    0x40A0FF, 0x40E0A0, 0xA0E040, 0xFFD040, 0xFF9040, 0xFF5060, 0xE060C0, 0xA070FF,
    0x60C0FF, 0xFFFFFF, 0x50FFD0, 0xC0FF60, 0xFFB0A0, 0xFF80E0, 0xB0A0FF, 0x80D0D0 };
static uint32_t mp3_chcol[16] = {                 /* the colours in use: C picks a theme (mp3_theme_apply) */
    0x40A0FF, 0x40E0A0, 0xA0E040, 0xFFD040, 0xFF9040, 0xFF5060, 0xE060C0, 0xA070FF,
    0x60C0FF, 0xFFFFFF, 0x50FFD0, 0xC0FF60, 0xFFB0A0, 0xFF80E0, 0xB0A0FF, 0x80D0D0 };

/* ---- the screen mode. MIDPLAY.CFG keeps the choice (MODE3D=w h bits, HZ3D=refresh; in a Windows DOS box MODE3DW /
 * HZ3DW, since a window can only show VGA 320x200). Auto: the best 640x480 mode in DOS, VGA 320x200 in Windows.
 * A mode that cannot be set falls back: the same mode at the BIOS's refresh, the last mode that worked, auto, VGA
 * 320x200, then the text views with a message. A mode chosen in Options (and the first VESA mode in a Windows DOS box,
 * where it may stay black) must be confirmed with Y within 10 s, any other key or the time goes back. */
static int    mp3_want_w, mp3_want_h, mp3_want_b, mp3_hz;      /* the choice (w 0 = auto) */
static int    mp3_cur = -1, mp3_ok_w, mp3_ok_h, mp3_ok_b, mp3_ok_hz;   /* set now / the last confirmed one */
static int    mp3_ask, mp3_win_ok;                            /* confirm the next mode set; a VESA mode was kept in Windows */
static double mp3_ask_until, mp3_anim;
static int    mp3_force;                                      /* draw the next frame now (a key was pressed) */
static int    mp3_win;                                        /* MIDPLAY runs in a Windows DOS box */
static int mp3_find(int w, int h, int b)
{
    int i;
    for (i = 0; i < m3_nmodes; i++) if (m3_modes[i].w == w && m3_modes[i].h == h && (!b || m3_modes[i].bpp == b)) return i;
    return -1;
}
static int mp3_auto(void)
{
    int i;
    if (mp3_win) return 0;                                    /* VGA 320x200: the one mode a window can show */
    for (i = 0; i < m3_nmodes; i++) if (m3_modes[i].mode == m3_vbest) return i;
    for (i = m3_nmodes - 1; i >= 0; i--) if (m3_modes[i].w == 640 && m3_modes[i].h == 480 && m3_modes[i].bpp > 8) return i;
    return 0;
}
static const char* mp3_mode_name(int i, char* b, size_t n)
{
    if (i < 0 || i >= m3_nmodes) { snprintf(b, n, "?"); return b; }
    if (m3_modes[i].mode == 0x13) snprintf(b, n, "320x200 VGA, 256 colours");
    else snprintf(b, n, "%dx%d %s%s", m3_modes[i].w, m3_modes[i].h,
                  m3_modes[i].bpp == 8 ? "256 colours" : m3_modes[i].bpp == 15 ? "15 bit" : m3_modes[i].bpp == 16 ? "16 bit" : m3_modes[i].bpp == 24 ? "24 bit" : "32 bit",
                  m3_modes[i].lfb ? "" : " (banked)");
    return b;
}
static int mp3_buffers(void)
{
    static int have;
    int need = M3W * M3H;
    if (need > have || !m3_fb || !m3_zb)
    {
        uint32_t* f = (uint32_t*)realloc(m3_fb, (size_t)need * 4); float* z;
        if (!f) return 0;
        m3_fb = f; z = (float*)realloc(m3_zb, (size_t)need * sizeof(float));
        if (!z) return 0;
        m3_zb = z; have = need;
    }
    m3_scr.w = M3W; m3_scr.h = M3H; m3_scr.p = m3_fb;
    return 1;
}

/* buffers and textures, made before the music starts (switching to 3D later then costs only the mode set) */
static void mp3_prepare(void)
{
    m3_W = 640; m3_H = 480;
    if (!mp3_buffers()) return;
    m3_theme_sync();
    if (!mp3_panel.p) mp3_panel = m3_surf(512, 128);
    if (!mp3_keys.p) mp3_keys = m3_surf(512, 64);
    if (m3_tsc_us == 0) m3_time_init();
}
static void mp3_make_panel(const char* title, double frac, int paused);
static int mp3_try(int i, int hz)
{
    if (i < 0 || i >= m3_nmodes) return 0;
    if (!m3_open_mode(&m3_modes[i], hz)) return 0;
    if (!mp3_buffers()) { m3_vbe_close(); return 0; }
    mp3_cur = i;
    return 1;
}
static int mp3_enter(void)
{
    int want, ok = 0;
    if (mp3_on) return 1;
    m3_vbe_probe();
    if (!mp3_panel.p) mp3_panel = m3_surf(512, 128);
    if (!mp3_keys.p) mp3_keys = m3_surf(512, 64);
    if (!mp3_panel.p || !mp3_keys.p) return 0;
    want = mp3_want_w ? mp3_find(mp3_want_w, mp3_want_h, mp3_want_b) : -1;
    if (want < 0) want = mp3_auto();
    ok = mp3_try(want, mp3_hz);
    if (!ok && mp3_ok_w) { ok = mp3_try(mp3_find(mp3_ok_w, mp3_ok_h, mp3_ok_b), mp3_ok_hz); if (ok) mp3_ask = 0; }
    if (!ok) { ok = mp3_try(mp3_auto(), 0); if (ok) mp3_ask = 0; }
    if (!ok) { ok = mp3_try(0, 0); if (ok) mp3_ask = 0; }
    if (!ok) { m3_W = 640; m3_H = 480; return 0; }
    m3_theme_sync();
    if (m3_tsc_us == 0) m3_time_init();
    mp3_slow = 0; mp3_on = 1; mp3_last_frame = 0; mp3_force = 1;
    if (!mp3_win && !m3_refresh) m3_refresh_seen = m3_measure_refresh();   /* (a Windows DOS box fakes the retrace) */
    if (mp3_win && m3_modes[mp3_cur].mode != 0x13 && !mp3_win_ok) mp3_ask = 1;   /* may be black: ask */
    mp3_ask_until = mp3_ask ? m3_now() + 10 : 0;
    if (!mp3_ask) { mp3_ok_w = m3_modes[mp3_cur].w; mp3_ok_h = m3_modes[mp3_cur].h; mp3_ok_b = m3_modes[mp3_cur].bpp; mp3_ok_hz = m3_refresh; }
    mp3_ask = 0;
    return 1;
}
static void mp3_leave(void)
{
    if (!mp3_on) return;
    m3_vbe_close();
    mp3_on = 0;
}
/* the answer to "keep this mode?": 1 = Y (keep), 0 = anything else or the time ran out (back to the last good one) */
static void mp3_answer(int keep)
{
    if (!mp3_ask_until) return;
    mp3_ask_until = 0;
    if (keep)
    {
        mp3_ok_w = m3_modes[mp3_cur].w; mp3_ok_h = m3_modes[mp3_cur].h; mp3_ok_b = m3_modes[mp3_cur].bpp; mp3_ok_hz = m3_refresh;
        if (mp3_win) mp3_win_ok = 1;
        return;
    }
    mp3_leave();
    mp3_want_w = mp3_ok_w; mp3_want_h = mp3_ok_h; mp3_want_b = mp3_ok_b; mp3_hz = mp3_ok_hz;
    if (mp3_win && !mp3_ok_w) { mp3_want_w = 320; mp3_want_h = 200; mp3_want_b = 8; }
    mp3_enter();
}

/* the module's face: brand, LCD with the synth's own display, knob ring, LEDs */
static void mp3_make_panel(const char* title, double frac, int paused)
{
    M3SURF* s = &mp3_panel; char line[64]; int x, y, i;
    if (!mp3_face.p)
    {
        M3SURF* f;
        mp3_face = m3_surf(512, 128); f = &mp3_face;
        for (y = 0; y < 128; y++) for (x = 0; x < 512; x++)
        { int v = 40 + ((x * 7 + y * 13) % 5) + ((y & 1) ? 2 : 0); f->p[y * 512 + x] = m3_rgb(v, v, v + 6); }
        m3_bevel(f, 0, 0, 512, 128, 3, 0, 120);
        m3_text(f, 12, 8, 2, "NUKED", 0xE8E8F0, 256);
        m3_text(f, 12, 36, 0, "SB-EMU", 0xB8B8C8, 256);
        m3_text(f, 12, 54, 0, "MIDI PLAYER", 0x9A9AA8, 256);
        m3_text(f, 440, 112, 0, "POSITION", 0x9090A0, 200);
    }
    memcpy(s->p, mp3_face.p, 512 * 128 * 4);
    M3_HOOK();
    m3_text(s, 12, 104, 0, paused ? "PAUSED" : "PLAYING", paused ? 0xFFB040 : 0x60FF70, 256);
    m3_rect(s, 74, 108, 8, 8, paused ? 0x604010 : ((int)(m3_now() * 4) & 1 ? 0x30FF40 : 0x108020), 256);
    /* LCD */
    m3_rect(s, 132, 8, 280, 112, 0x101810, 256);
    m3_bevel(s, 132, 8, 280, 112, 2, 1, 200);
    m3_vgrad(s, 136, 12, 272, 104, 0xA8E048, 0x7CB830, 256);
    if (g_lcd_ok && g_lcd[2] != 2)
    {   /* the synth's own display: part, instrument, values and the 16-part level dots */
        char part[4], ins[17]; int c, r;
        for (i = 0; i < 3; i++) { unsigned char ch = g_lcd[6 + i]; part[i] = ch >= 32 && ch < 127 ? ch : ' '; } part[3] = 0;
        for (i = 0; i < 16; i++) { unsigned char ch = g_lcd[9 + i]; ins[i] = ch >= 32 && ch < 127 ? ch : ' '; } ins[16] = 0;
        snprintf(line, sizeof line, "PART %s", part);  m3_text(s, 142, 15, 0, line, 0x14240A, 256);
        m3_text(s, 142, 33, 0, ins, 0x0C1A06, 256);
        M3_HOOK();
        for (c = 0; c < 16; c++) for (r = 0; r < 16; r++)
            if (lcd_dot(r, c)) m3_rect(s, 286 + c * 7, 16 + r * 6, 5, 4, 0x10200A, 256);
            else m3_rect(s, 286 + c * 7, 16 + r * 6, 5, 4, 0x10200A, 30);
    }
    else
    {   /* MT-32 / SoundFont or no answer: our own meters from the MIDI we send */
        int c, r;
        m3_text(s, 142, 15, 0, "MIDI PLAYER", 0x14240A, 256);
        for (c = 0; c < 16; c++)
        {
            int h = g_viz.level[c] * 16 / 128;
            for (r = 0; r < 16; r++) m3_rect(s, 286 + c * 7, 16 + r * 6, 5, 4, 0x10200A, 15 - r < h ? 256 : 30);
        }
    }
    snprintf(line, sizeof line, "%.17s", title); m3_text(s, 142, 60, 0, line, 0x10200A, 256);
    if (g_lcd_load >= 0) { snprintf(line, sizeof line, "LOAD %3d%%", (g_lcd_load + 5) / 10); m3_text(s, 142, 78, 0, line, 0x2A3C18, 256); }
    snprintf(line, sizeof line, "%ld BPM", g_viz.bpm10 / 10); m3_text(s, 142, 96, 0, line, 0x2A3C18, 256);
    /* knob ring: song position */
    for (i = 0; i < 24; i++)
    {
        double a = (0.75 + i / 24.0 * 1.5) * 3.14159265;   /* 7 o'clock to 5 o'clock */
        int px = 464 + (int)(48 * cos(a)), py = 64 + (int)(48 * sin(a));
        m3_rect(s, px - 2, py - 2, 4, 4, i <= frac * 23 ? 0xFFA030 : 0x404048, 256);
    }
}

/* keyboard texture: 75 white keys (C0..), lit by the channel that played the note last */
static void mp3_make_keys(void)
{
    M3SURF* s = &mp3_keys; int n, x, wk = 0;
    static const int black[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
    m3_rect(s, 0, 0, 512, 64, 0x101014, 256);
    M3_HOOK();
    for (n = 12; n < 120; n++) if (!black[n % 12])
    {   /* white keys, 63 of them across 512 pixels */
        int lv = g_viz.nl[n]; uint32_t c = 0xE8E8EC;
        x = wk * 512 / 63;
        if (lv > 0) c = m3_lerpc(0xE8E8EC, mp3_chcol[g_viz.ncol[n] & 15], 80 + lv);
        m3_rect(s, x + 1, 1, 512 / 63 - 1, 62, c, 256);
        wk++;
    }
    wk = 0;
    M3_HOOK();
    for (n = 12; n < 120; n++)
    {
        if (!black[n % 12]) { wk++; continue; }
        {   /* black keys sit between the white ones */
            int lv = g_viz.nl[n]; uint32_t c = 0x18181C;
            x = wk * 512 / 63 - 3;
            if (lv > 0) c = m3_lerpc(0x18181C, mp3_chcol[g_viz.ncol[n] & 15], 80 + lv);
            m3_rect(s, x, 1, 6, 38, c, 256);
        }
    }
}

static void mp3_bar(const char* name, const char* title, uint64_t el_us, uint64_t total_us, int paused, int done)
{
    char b[160], t1[16], t2[16]; int w, n;
    unsigned long el = (unsigned long)((el_us > total_us ? total_us : el_us) / 1000000ULL);
    m3_rect(&m3_scr, 0, 0, M3W, 24, 0x000000, 150);
    m3_text_sh(&m3_scr, 8, 4, 0, "MIDPLAY", 0xFFFFFF);
    snprintf(b, sizeof b, "%.30s%s%.28s", name, title[0] ? "  -  " : "", title);
    m3_text(&m3_scr, 80, 4, 0, b, 0xB0D0FF, 256);
    snprintf(b, sizeof b, "Song %d/%d", g_cur + 1, g_npl);
    m3_text(&m3_scr, M3W - 8 - (int)strlen(b) * 8, 4, 0, b, 0x9090A8, 256);
    {   /* time bar; small screens (VGA 320x200) get a slimmer band without the key line */
        int bh = M3H >= 400 ? 46 : 22, ty = M3H - bh + 4;
        m3_rect(&m3_scr, 0, M3H - bh, M3W, bh, 0x000000, 160);
        fmt_time(t1, (unsigned long)(total_us / 1000000ULL)); fmt_time(t2, el);
        m3_text(&m3_scr, 6, ty, 0, t2, 0xFFFFFF, 256);
        m3_text(&m3_scr, M3W - 8 * (int)strlen(t1) - 6, ty, 0, t1, 0xFFFFFF, 256);
        w = M3W - 140; n = total_us ? (int)((el_us > total_us ? total_us : el_us) * (uint64_t)w / total_us) : 0;
        m3_rect(&m3_scr, 66, ty + 4, w, 8, 0x303040, 256);
        m3_vgrad(&m3_scr, 66, ty + 4, n, 8, 0x60FFA0, 0x20A060, 256);
    }
    if (paused) m3_text_sh(&m3_scr, M3W / 2 - 28, M3H - (M3H >= 400 ? 64 : 40), 0, "PAUSED", 0xFFC040);
    else if (done) m3_text_sh(&m3_scr, M3W / 2 - 20, M3H - (M3H >= 400 ? 64 : 40), 0, "DONE", 0x60FF60);
    if (mp3_slow) m3_text_sh(&m3_scr, 8, 28, 0, M3W >= 640 ? "Few frames on this PC: F4 half resolution (faster), S text views." : "Slow: F4 half resolution", 0xFFC040);
    if (M3H >= 400) m3_text(&m3_scr, 10, M3H - 22, 0, M3W >= 720 ? "SPACE pause  N/P song  <> seek  TAB lists  S view  F4 resolution  F2 screen mode  ESC quit"
                                    : M3W >= 640 ? "SPACE pause  N/P song  <> seek  TAB lists  S view  F4 resolution  F1 help  ESC quit"
                                    : "SPACE pause  N/P song  S view  TAB lists  ESC quit", 0xA0A0B8, 256);
    if (mp3_ask_until)
    {   /* a new screen mode: keep it? (if the screen is black, any key or 10 s brings the last one back) */
        char q[80]; int left = (int)(mp3_ask_until - m3_now()) + 1, x0 = M3W / 2 - 150; if (left < 0) left = 0; if (x0 < 4) x0 = 4;
        m3_rect(&m3_scr, x0 - 6, M3H / 2 - 30, 312 > M3W - 8 ? M3W - 8 : 312, 52, 0x000000, 210);
        snprintf(q, sizeof q, "%dx%d%s%s", M3W, M3H, m3_refresh ? " at " : "", ""); if (m3_refresh) snprintf(q + strlen(q), sizeof q - strlen(q), "%d Hz", m3_refresh);
        m3_text_sh(&m3_scr, x0, M3H / 2 - 26, 0, q, 0xFFFFFF);
        snprintf(q, sizeof q, "Keep this mode? Y = yes (%d s)", left);
        m3_text_sh(&m3_scr, x0, M3H / 2 - 6, 0, q, 0xFFC040);
    }
}

/* ===================================================================== looks, colour themes and players (V C J) ==
 * V: solid (the 3D view as it was), vector (glowing lines on black with an afterglow, like an arcade vector monitor;
 * smooth Wu lines, always full resolution: lines are cheap) and voxel (everything built from blocks, notes fall as
 * blocks onto the keys). C: colour themes (channel colours, sky, floor). J: the player in the middle (the NUKED
 * module, a boombox, a jukebox, a turntable); each is a few boxes, drawn solid, as lines or as blocks. */
static const char* const mp3_scene_name[3] = { "solid", "vector", "voxel" };
static const char* const mp3_theme_name[7] = { "classic", "neon", "amber CRT", "green phosphor", "sunset", "ice", "synthwave" };
static const char* const mp3_model_name[4] = { "NUKED module", "boombox", "jukebox", "turntable" };

static uint32_t mp3_hsv(float h, float sat, float v)
{
    float r, g, b, f, p, q, t; int i;
    h = fmodf(h, 360.0f); if (h < 0) h += 360.0f; h /= 60.0f; i = (int)h; f = h - i;
    p = v * (1 - sat); q = v * (1 - sat * f); t = v * (1 - sat * (1 - f));
    switch (i) { case 0: r = v; g = t; b = p; break; case 1: r = q; g = v; b = p; break; case 2: r = p; g = v; b = t; break;
                 case 3: r = p; g = q; b = v; break; case 4: r = t; g = p; b = v; break; default: r = v; g = p; b = q; }
    return (uint32_t)(r * 255) << 16 | (uint32_t)(g * 255) << 8 | (uint32_t)(b * 255);
}
/* after the F3 style set the scene's colours (m3_theme_sync), a theme other than classic paints over them */
static void mp3_theme_apply(void)
{
    static int last = -1, lastgen = -1; int c;
    /* hue start, hue span, saturation, sky top, sky bottom, grid */
    static const struct { float h0, hs, sat; uint32_t s0, s1, grid; } T[7] = {
        { 0, 0, 0, 0, 0, 0 },
        { 0, 330, 1.0f, 0x05001A, 0x2A0050, 0xFF30D0 },
        { 22, 22, 0.95f, 0x0C0600, 0x2A1400, 0xFFA020 },
        { 100, 40, 0.9f, 0x000A02, 0x002A08, 0x30FF60 },
        { 300, 110, 0.85f, 0x1A0830, 0xC04830, 0xFF8040 },
        { 170, 70, 0.55f, 0x08142A, 0x6090C0, 0xC0F0FF },
        { 270, 110, 0.9f, 0x0A0018, 0x500048, 0x20F0FF } };
    if (g_ctheme == last && m3_theme_gen == lastgen) return;
    last = g_ctheme;
    for (c = 0; c < 16; c++)
        mp3_chcol[c] = g_ctheme ? mp3_hsv(T[g_ctheme].h0 + T[g_ctheme].hs * ((c * 7) % 16) / 15.0f, T[g_ctheme].sat, 0.75f + 0.25f * (c & 1)) : mp3_chcol0[c];
    if (g_ctheme)
    {
        m3_sky0 = T[g_ctheme].s0; m3_sky1 = T[g_ctheme].s1; m3_gridc = m3_glowc = T[g_ctheme].grid;
        m3_floorbg = m3_mul(T[g_ctheme].s0, 300); m3_fogc = m3_mul(T[g_ctheme].s1, 160);
        m3_make_floor();
    }
    else if (lastgen >= 0) { m3_theme_from_pal(tui_palette()); }
    lastgen = m3_theme_gen;
}

/* ---- the players: boxes. kind 0 plain, 1 the screen (the LCD texture on its front), 2 a speaker / knob facing us
 * (hx = radius), 3 a light strip in channel colours, 4 the turntable's platter (turns) */
typedef struct { float x, y, z, hx, hy, hz; uint32_t col; int kind; } MP3BOX;
static int mp3_model(MP3BOX* b, float ta)
{
    int n = 0; float fz = 0.6f;
#define MB(X, Y, Z, HX, HY, HZ, C, K) (b[n].x = (X), b[n].y = (Y), b[n].z = (Z), b[n].hx = (HX), b[n].hy = (HY), b[n].hz = (HZ), b[n].col = (C), b[n].kind = (K), n++)
    switch (g_model)
    {
    case 1:     /* boombox: body, cassette window (the LCD), two speakers, a handle */
        MB(0, 0.25f, fz, 2.4f, 0.7f, 0.55f, 0x2C2C34, 0);
        MB(0, 0.3f, fz - 0.56f, 0.75f, 0.38f, 0.02f, 0, 1);
        MB(-1.55f, 0.22f, fz - 0.57f, 0.55f, 0, 0.1f, 0x18181C, 2);
        MB(1.55f, 0.22f, fz - 0.57f, 0.55f, 0, 0.1f, 0x18181C, 2);
        MB(-1.6f, 1.15f, fz, 0.08f, 0.22f, 0.08f, 0x505058, 0);
        MB(1.6f, 1.15f, fz, 0.08f, 0.22f, 0.08f, 0x505058, 0);
        MB(0, 1.42f, fz, 1.68f, 0.06f, 0.08f, 0x505058, 0);
        break;
    case 2:     /* jukebox: tall cabinet, rounded top in steps, window (the LCD), speaker, light strips */
        MB(0, 0.45f, fz, 1.3f, 1.05f, 0.6f, 0x6A2418, 0);
        MB(0, 1.62f, fz, 1.1f, 0.12f, 0.55f, 0x7A2C1C, 0);
        MB(0, 1.84f, fz, 0.8f, 0.1f, 0.5f, 0x8A3420, 0);
        MB(0, 2.0f, fz, 0.45f, 0.06f, 0.45f, 0x9A3C24, 0);
        MB(0, 0.85f, fz - 0.61f, 0.95f, 0.38f, 0.02f, 0, 1);
        MB(0, -0.15f, fz - 0.62f, 0.42f, 0, 0.08f, 0x201010, 2);
        MB(-1.18f, 0.6f, fz - 0.62f, 0.06f, 0.95f, 0.04f, 0, 3);
        MB(1.18f, 0.6f, fz - 0.62f, 0.06f, 0.95f, 0.04f, 0, 3);
        break;
    case 3:     /* turntable: plinth, platter (turns), tonearm, a small display */
        MB(0, -0.2f, fz - 0.4f, 2.4f, 0.16f, 1.1f, 0x34343C, 0);
        MB(-0.55f, 0.0f, fz - 0.4f, 0.95f, 0.04f, 0.95f, 0x141418, 4);
        MB(-0.55f, 0.06f, fz - 0.4f, 0.25f, 0.03f, 0.25f, 0xC03020, 4);
        MB(1.25f, 0.12f, fz - 0.05f, 0.08f, 0.16f, 0.08f, 0xA0A0A8, 0);
        MB(0.85f, 0.3f, fz - 0.5f, 0.42f, 0.025f, 0.03f, 0xC0C0C8, 0);
        MB(1.55f, 0.1f, fz - 1.52f, 0.6f, 0.18f, 0.02f, 0, 1);
        break;
    default:    /* the NUKED module */
        MB(0, 0.35f, fz, 2.6f, 0.62f, 1.2f, 0x26262C, 1);
        MB(-2.72f, 0.35f, fz - 1.17f, 0.12f, 0.62f, 0.03f, 0x303036, 0);
        MB(2.72f, 0.35f, fz - 1.17f, 0.12f, 0.62f, 0.03f, 0x303036, 0);
        MB(-2.6f + (464 / 512.0f) * 5.2f, 0.35f + 0.62f - 0.62f, fz - 1.36f, 0.32f, 0, 0.16f, 0x303038, 2);
        break;
    }
#undef MB
    (void)ta;
    return n;
}

/* ---- vector drawing: smooth (Wu) lines added onto the picture, which fades a little every frame (afterglow) */
static void mp3_addpx(int x, int y, uint32_t c, int a)
{
    uint32_t* p; int r, g, b;
    if ((unsigned)x >= (unsigned)m3_rw || (unsigned)y >= (unsigned)m3_rh || a <= 0) return;
    p = &m3_fb[y * M3W + x];
    r = (*p >> 16 & 255) + ((c >> 16 & 255) * a >> 8); g = (*p >> 8 & 255) + ((c >> 8 & 255) * a >> 8); b = (*p & 255) + ((c & 255) * a >> 8);
    *p = (uint32_t)(r > 255 ? 255 : r) << 16 | (uint32_t)(g > 255 ? 255 : g) << 8 | (uint32_t)(b > 255 ? 255 : b);
}
static void mp3_wline(float x0, float y0, float x1, float y1, uint32_t c)
{
    int steep = fabsf(y1 - y0) > fabsf(x1 - x0), x, xe; float t, gr, y;
    if (steep) { t = x0; x0 = y0; y0 = t; t = x1; x1 = y1; y1 = t; }
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    if (x1 - x0 < 0.5f) return;
    if (x0 < -2000 || x1 > 4000) return;
    gr = (y1 - y0) / (x1 - x0);
    x = (int)(x0 + 0.5f); xe = (int)(x1 + 0.5f); y = y0 + gr * (x - x0);
    for (; x <= xe; x++, y += gr)
    {
        int yi = (int)floorf(y); int a = (int)((y - yi) * 256);
        if (steep) { mp3_addpx(yi, x, c, 256 - a); mp3_addpx(yi + 1, x, c, a); }
        else { mp3_addpx(x, yi, c, 256 - a); mp3_addpx(x, yi + 1, c, a); }
    }
}
static void mp3_line3(M3V a, M3V b, uint32_t c)
{
    M3P p = m3_xf(a, 0, 0, 1), q = m3_xf(b, 0, 0, 1); float f = m3_f * m3_rw / M3_FBASE, nz = 0.1f;
    if (p.z < nz && q.z < nz) return;
    if (p.z < nz) { float t = (nz - p.z) / (q.z - p.z); p.x += (q.x - p.x) * t; p.y += (q.y - p.y) * t; p.z = nz; }
    if (q.z < nz) { float t = (nz - q.z) / (p.z - q.z); q.x += (p.x - q.x) * t; q.y += (p.y - q.y) * t; q.z = nz; }
    mp3_wline(m3_rw / 2 + p.x * f / p.z, m3_rh / 2 - p.y * f * m3_fyk / p.z, m3_rw / 2 + q.x * f / q.z, m3_rh / 2 - q.y * f * m3_fyk / q.z, c);
}
static void mp3_wbox(float x, float y, float z, float hx, float hy, float hz, float yaw, uint32_t c)
{
    static const int e[12][2] = { {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7} };
    static const int k[8][3] = { {-1,1,-1},{1,1,-1},{1,-1,-1},{-1,-1,-1},{-1,1,1},{1,1,1},{1,-1,1},{-1,-1,1} };
    M3V v[8]; int i; float cy = cosf(yaw), sy = sinf(yaw);
    for (i = 0; i < 8; i++)
    {
        float px = k[i][0] * hx, py = k[i][1] * hy, pz = k[i][2] * hz;
        v[i] = m3_v(x + px * cy + pz * sy, y + py, z - px * sy + pz * cy);
    }
    for (i = 0; i < 12; i++) mp3_line3(v[e[i][0]], v[e[i][1]], c);
}
static void mp3_wcircle(float x, float y, float z, float r, float rot, uint32_t c)
{
    int i; M3V p0 = m3_v(x + r, y, z);
    for (i = 1; i <= 24; i++)
    {
        float a = i * 6.2831853f / 24; M3V p1 = m3_v(x + r * cosf(a), y + r * sinf(a), z);
        mp3_line3(p0, p1, c); p0 = p1;
    }
    mp3_line3(m3_v(x, y, z), m3_v(x + r * cosf(rot), y + r * sinf(rot), z), c);
}

/* the keys' positions on the keyboard quad (x from -2.8 to 2.8): note -> centre x, black or not */
static float mp3_keyx(int n, int* black)
{
    static const int bl[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
    int i, wk = 0;
    for (i = 12; i < n; i++) if (!bl[i % 12]) wk++;
    *black = bl[n % 12];
    return -2.8f + (wk + (*black ? 0.0f : 0.5f)) * 5.6f / 63;
}

/* ---- voxel: falling note blocks */
typedef struct { float x, y, v; uint32_t c; int on; } MP3DROP;
static MP3DROP mp3_drop[48];
static unsigned char mp3_nlast[128];

static void mp3_cube(float x, float y, float z, float h, uint32_t c, float glow) { m3_box(m3_v(x, y, z), h, h, h, 0, NULL, c, glow, 256); }

static void mp3_scene_rest(const Song* s, const char* name, double frac, int paused, float ta, float dt)
{
    MP3BOX b[12]; int n = mp3_model(b, ta), i, c, k;
    float H = 0.62f;
    if (g_scene == 1)
    {   /* ---------------------------------------------------- vector */
        uint32_t acc = g_ctheme ? m3_gridc : 0x40FF90, dim = m3_mul(acc, 110);
        float off = fmodf(ta * 1.5f, 1.0f), fy = -H - 0.02f;
        for (i = -10; i <= 10; i++) mp3_line3(m3_v(i * 1.0f, fy, -5.5f), m3_v(i * 1.0f, fy, 22), dim);
        for (i = 0; i < 26; i++) { float z = -5.5f + i - off; mp3_line3(m3_v(-10, fy, z), m3_v(10, fy, z), dim); }
        M3_HOOK();
        for (i = 0; i < n; i++)
        {
            MP3BOX* q = &b[i];
            if (q->kind == 2) { mp3_wcircle(q->x, q->y, q->z, q->hx, (float)(frac * 6.28), acc); mp3_wcircle(q->x, q->y, q->z, q->hx * 0.55f, 0, dim); }
            else if (q->kind == 4) mp3_wbox(q->x, q->y, q->z, q->hx, q->hy, q->hz, ta * 2.2f, acc);
            else if (q->kind == 3) { for (c = 0; c < 8; c++) mp3_wbox(q->x, q->y - q->hy + (c + 0.5f) * q->hy / 4, q->z, q->hx, q->hy / 9, q->hz, 0, g_viz.level[c * 2] > 20 ? mp3_chcol[c * 2] : dim); }
            else if (q->kind == 1)
            {   /* the screen: its frame and the 16 channel levels as bars */
                mp3_wbox(q->x, q->y, q->z, q->hx, q->hy, 0.01f, 0, acc);
                for (c = 0; c < 16; c++)
                {
                    float bx = q->x - q->hx * 0.9f + c * q->hx * 1.8f / 15, h = q->hy * 1.6f * g_viz.level[c] / 127.0f;
                    mp3_line3(m3_v(bx, q->y - q->hy * 0.8f, q->z - 0.02f), m3_v(bx, q->y - q->hy * 0.8f + h, q->z - 0.02f), mp3_chcol[c]);
                }
                if (g_model == 0) mp3_wbox(q->x, q->y, q->z, q->hx, q->hy, q->hz, 0, acc);
            }
            else mp3_wbox(q->x, q->y, q->z, q->hx, q->hy, q->hz, 0, acc);
        }
        M3_HOOK();
        /* the keyboard: its outline, each note that plays shoots a beam up in its channel's colour */
        mp3_line3(m3_v(-2.8f, -0.34f, -0.95f), m3_v(2.8f, -0.34f, -0.95f), acc); mp3_line3(m3_v(-2.8f, -0.62f, -1.75f), m3_v(2.8f, -0.62f, -1.75f), acc);
        mp3_line3(m3_v(-2.8f, -0.34f, -0.95f), m3_v(-2.8f, -0.62f, -1.75f), acc); mp3_line3(m3_v(2.8f, -0.34f, -0.95f), m3_v(2.8f, -0.62f, -1.75f), acc);
        for (k = 12; k < 120; k++)
        {
            int bl; float x = mp3_keyx(k, &bl);
            if (!bl && !(k % 12)) mp3_line3(m3_v(x - 2.8f / 63, -0.34f, -0.95f), m3_v(x - 2.8f / 63, -0.62f, -1.75f), dim);
            if (g_viz.nl[k] > 0)
            {
                uint32_t cc = mp3_chcol[g_viz.ncol[k] & 15]; float h = 0.2f + g_viz.nl[k] / 127.0f * 1.4f, z = bl ? -1.15f : -1.4f, y = bl ? -0.42f : -0.5f;
                mp3_line3(m3_v(x, y, z), m3_v(x, y + h, z + 0.3f), cc);
            }
        }
        M3_HOOK();
        for (c = 0; c < 16; c++)
        {
            float target = 0.03f + g_viz.level[c] / 127.0f * 0.36f;
            mp3_col[c] += (target - mp3_col[c]) * (1 - expf(-dt * 12));
            mp3_wbox(-1.8f + c * 0.24f, -H - 0.02f + mp3_col[c] / 2, -3.5f, 0.085f, mp3_col[c] / 2, 0.085f, 0, mp3_chcol[c]);
        }
        return;
    }
    if (g_scene == 2)
    {   /* ---------------------------------------------------- voxel */
        float q = 0.11f;
        mp3_make_panel(s->title[0] ? s->title : base_of(name), frac, paused);
        for (i = 0; i < n; i++)
        {
            MP3BOX* m = &b[i];
            if (m->kind == 1)
            {   /* the screen as big pixels: the LCD texture sampled into blocks, the lit ones stand out */
                int gx = (int)(m->hx * 2 / (q * 2)), gy = (int)(m->hy * 2 / (q * 2)), x, y;
                if (gx > 36) gx = 36; if (gy > 12) gy = 12;
                if (g_model == 0) m3_box(m3_v(m->x, m->y, m->z), m->hx, m->hy, m->hz, 0, NULL, 0x26262C, 0, 256);
                for (y = 0; y < gy; y++)
                {
                    M3_HOOK();
                    for (x = 0; x < gx; x++)
                    {
                        uint32_t cc = mp3_panel.p[(int)((y + 0.5f) * mp3_panel.h / gy) * mp3_panel.w + (int)((x + 0.5f) * mp3_panel.w / gx)];
                        int lum = (cc >> 16 & 255) + (cc >> 8 & 255) + (cc & 255);
                        float bx = m->x - m->hx + (x + 0.5f) * m->hx * 2 / gx, by = m->y + m->hy - (y + 0.5f) * m->hy * 2 / gy;
                        mp3_cube(bx, by, m->z - m->hz - (lum > 300 ? q * 0.6f : 0), m->hx / gx * 0.92f, cc, lum > 300 ? 0.3f : 0);
                    }
                }
            }
            else if (m->kind == 2)
            {   /* a disc of blocks */
                int r = (int)(m->hx / q), x, y;
                for (y = -r; y <= r; y++) for (x = -r; x <= r; x++)
                    if (x * x + y * y <= r * r)
                    {
                        int ring = (x * x + y * y) * 4 / (r * r + 1);
                        float pump = ring == 0 ? 0.04f * (g_viz.level[9] / 127.0f) : 0;
                        mp3_cube(m->x + x * q, m->y + y * q, m->z - pump, q * 0.48f, m3_mul(m->col, 140 + ring * 40), 0);
                    }
            }
            else if (m->kind == 3)
            {
                for (c = 0; c < 8; c++)
                    mp3_cube(m->x, m->y - m->hy + (c + 0.5f) * m->hy / 4, m->z, m->hy / 9, g_viz.level[c * 2] > 20 ? mp3_chcol[c * 2] : 0x302020, g_viz.level[c * 2] > 20 ? 0.6f : 0);
            }
            else m3_box(m3_v(m->x, m->y, m->z), m->hx, m->hy, m->hz, m->kind == 4 ? ta * 2.2f : 0, NULL, m->col, 0, 256);
        }
        M3_HOOK();
        /* the keys as blocks; a playing key sinks and glows */
        for (k = 21; k < 109; k++)
        {
            int bl; float x = mp3_keyx(k, &bl), lv = g_viz.nl[k] / 127.0f;
            uint32_t cc = g_viz.nl[k] > 0 ? mp3_chcol[g_viz.ncol[k] & 15] : bl ? 0x202024 : 0xE0E0E4;
            mp3_cube(x, (bl ? -0.36f : -0.48f) - lv * 0.05f, bl ? -1.2f : -1.38f, bl ? 0.03f : 0.042f, cc, lv > 0 ? 0.4f : 0);
            if (g_viz.nl[k] > mp3_nlast[k] + 8)
            {   /* a new note: a block drops onto its key */
                for (i = 0; i < 48 && mp3_drop[i].on; i++) {}
                if (i < 48) { mp3_drop[i].on = 1; mp3_drop[i].x = x; mp3_drop[i].y = 1.6f; mp3_drop[i].v = 0; mp3_drop[i].c = cc; }
            }
            mp3_nlast[k] = (unsigned char)g_viz.nl[k];
        }
        for (i = 0; i < 48; i++) if (mp3_drop[i].on)
        {
            MP3DROP* d = &mp3_drop[i];
            d->v += dt * 6; d->y -= d->v * dt;
            if (d->y < -0.4f) d->on = 0;
            else mp3_cube(d->x, d->y, -1.3f, 0.045f, d->c, 0.5f);
        }
        M3_HOOK();
        /* the channels as stacks of blocks */
        for (c = 0; c < 16; c++)
        {
            int h = 1 + (g_viz.level[c] * 8 + 63) / 127;
            for (k = 0; k < h; k++) mp3_cube(-1.8f + c * 0.24f, -H + 0.05f + k * 0.1f, -3.5f, 0.045f, mp3_chcol[c], k == h - 1 ? 0.5f : 0.1f);
        }
        return;
    }
    /* ---------------------------------------------------- solid, another player than the module */
    mp3_make_panel(s->title[0] ? s->title : base_of(name), frac, paused);
    for (i = 0; i < n; i++)
    {
        MP3BOX* m = &b[i];
        if (m->kind == 1) m3_box(m3_v(m->x, m->y, m->z), m->hx, m->hy, m->hz, 0, &mp3_panel, 0x26262C, 0, 256);
        else if (m->kind == 2) m3_knob3d(m3_v(m->x, m->y, m->z), m->hx, m->hz, (float)(frac * 6.28), 0.02f * g_viz.level[9] / 127.0f);
        else if (m->kind == 3) { for (c = 0; c < 8; c++) m3_box(m3_v(m->x, m->y - m->hy + (c + 0.5f) * m->hy / 4, m->z), m->hx, m->hy / 9, m->hz, 0, NULL, g_viz.level[c * 2] > 20 ? mp3_chcol[c * 2] : 0x402020, g_viz.level[c * 2] > 20 ? 0.6f : 0, 256); }
        else m3_box(m3_v(m->x, m->y, m->z), m->hx, m->hy, m->hz, m->kind == 4 ? ta * 2.2f : 0, NULL, m->col, 0, 256);
    }
    M3_HOOK();
    mp3_make_keys();
    m3_quad(m3_v(-2.8f, -0.34f, -0.95f), m3_v(2.8f, -0.34f, -0.95f), m3_v(2.8f, -0.62f, -1.75f), m3_v(-2.8f, -0.62f, -1.75f), &mp3_keys, 0xFFFFFF, 0.15f, 256, 0);
    for (c = 0; c < 16; c++)
    {
        float target = 0.03f + g_viz.level[c] / 127.0f * 0.36f;
        mp3_col[c] += (target - mp3_col[c]) * (1 - expf(-dt * 12));
        m3_box(m3_v(-1.8f + c * 0.24f, -H - 0.02f + mp3_col[c] / 2, -3.5f), 0.085f, mp3_col[c] / 2, 0.085f, 0, NULL, mp3_chcol[c], g_viz.count[c] > 0 ? 0.5f : 0.12f, 256);
    }
}

static void mp3_draw(const Song* s, const char* name, uint64_t el_us, uint64_t total_us, int paused, int done)
{
    double t = m3_now(), t0, hz, period, ta; float W = 2.6f, H = 0.62f, D = 1.2f, dt; int c, idle = paused || done || !s->nev;
    double frac = total_us ? (double)(el_us > total_us ? total_us : el_us) / (double)total_us : 0;
    if (!mp3_on) return;
    if (mp3_ask_until && t > mp3_ask_until) { mp3_answer(0); return; }
    /* pacing: at most one frame per screen refresh (the rate set, else the one measured, else 60 Hz); paused, done or
       no song: nothing moves, so a frame every 0.25 s (the LCD, the PAUSED blink, the confirm countdown) */
    hz = m3_refresh ? m3_refresh : (m3_refresh_seen >= 40 && m3_refresh_seen <= 200 ? m3_refresh_seen : 60);
    period = idle ? 0.25 : 1.0 / hz;
    if (!mp3_force && t - mp3_last_frame < period - 0.002) return;
    mp3_force = 0;
    m3_theme_sync();                          /* F3 (style) re-colours the scene too */
    dt = mp3_last_frame ? (float)(t - mp3_last_frame) : 0.06f; if (dt > 0.2f) dt = 0.2f;
    mp3_last_frame = t; t0 = t;
    if (!idle) mp3_anim += dt;                /* the scene's own clock stops while nothing plays */
    ta = mp3_anim;
    lcd_query();
    m3_bgphase = (float)ta;
    m3_fog = m3_fogc; m3_fog0 = 8; m3_fog1 = 24;
    mp3_theme_apply();                        /* C: colour theme over the style's colours */
    m3_camera(0.6f * sinf((float)ta * 0.2f), 0.55f + 0.08f * sinf((float)ta * 0.13f) + (g_model == 2 ? 0.35f : 0), -6.6f - (g_model == 2 ? 0.6f : 0), -0.1f * sinf((float)ta * 0.2f), -0.16f);
    if (g_scene == 1)
    {   /* vector: always full resolution; last frame fades to 5/8 (afterglow) instead of a new sky */
        uint32_t* p = m3_fb; int i2, n2 = M3W * M3H;
        m3_rw = M3W; m3_rh = M3H;
        for (i2 = 0; i2 < n2; i2++) { uint32_t v = p[i2]; p[i2] = (v >> 1 & 0x7F7F7F) + (v >> 3 & 0x1F1F1F); if (!(i2 & 0x7FFF)) M3_HOOK(); }
    }
    else
    {
        if (m3_rw != (m3_half ? M3W / 2 : M3W)) { m3_rw = m3_half ? M3W / 2 : M3W; m3_rh = m3_half ? M3H / 2 : M3H; }
        m3_sky();
        M3_HOOK();
        m3_draw_floor(-H - 0.02f, 256);
    }
    M3_HOOK();
    if (g_scene || g_model)
    {
        mp3_scene_rest(s, name, frac, paused, (float)ta, dt);
        M3_HOOK();
        if (g_scene != 1) m3_upscale();
        goto bar;
    }
    /* the module */
    mp3_make_panel(s->title[0] ? s->title : base_of(name), frac, paused);
    m3_box(m3_v(0, 0.35f, 0.6f), W, H, D, 0, &mp3_panel, 0x26262C, 0, 256);
    m3_box(m3_v(-W - 0.12f, 0.35f, 0.6f - D + 0.03f), 0.12f, H, 0.03f, 0, NULL, 0x303036, 0, 256);
    m3_box(m3_v(W + 0.12f, 0.35f, 0.6f - D + 0.03f), 0.12f, H, 0.03f, 0, NULL, 0x303036, 0, 256);
    m3_knob3d(m3_v(-W + (464 / 512.0f) * 2 * W, 0.35f + H - (64 / 128.0f) * 2 * H, 0.6f - D - 0.16f), 0.32f, 0.16f,
              (float)(frac * 4.7 - 2.35), 0);
    /* the keyboard, tilted towards the camera in front of the module */
    M3_HOOK();
    mp3_make_keys();
    M3_HOOK();
    m3_quad(m3_v(-2.8f, -0.34f, -0.95f), m3_v(2.8f, -0.34f, -0.95f), m3_v(2.8f, -0.62f, -1.75f), m3_v(-2.8f, -0.62f, -1.75f),
            &mp3_keys, 0xFFFFFF, 0.15f, 256, 0);
    /* 16 channel columns on the floor in front (low enough not to hide the keyboard) */
    for (c = 0; c < 16; c++)
    {
        float target = 0.03f + g_viz.level[c] / 127.0f * 0.36f, h;
        mp3_col[c] += (target - mp3_col[c]) * (1 - expf(-dt * 12));
        h = mp3_col[c];
        m3_box(m3_v(-1.8f + c * 0.24f, -H - 0.02f + h / 2, -3.5f), 0.085f, h / 2, 0.085f, 0, NULL,
               mp3_chcol[c], g_viz.count[c] > 0 ? 0.5f : 0.12f, 256);
    }
    M3_HOOK();
    m3_upscale();
bar:
    mp3_bar(base_of(name), s->title, el_us, total_us, paused, done);
    M3_HOOK();
    if (!mp3_win && (m3_now() - t0) < 0.75 / hz)
    {   /* drawn in well under a refresh: show it at the start of the retrace (no tearing); bounded, music keeps going */
        double lim = m3_now() + 1.0 / hz;
        while ((inportb(0x3DA) & 8) && m3_now() < lim) M3_HOOK();
        while (!(inportb(0x3DA) & 8) && m3_now() < lim) M3_HOOK();
    }
    m3_present();
    /* frame time: half resolution when slow; a note when even that is too slow */
    mp3_frame_ms = (m3_now() - t0) * 1000;
    st_frame((uint64_t)(mp3_frame_ms * 1000));
    /* always full resolution (sharp): the music no longer waits for frames, so a slow PC only gets fewer of
     * them (F4 still switches to half resolution by hand). A note when it is down to ~2 frames a second. */
    mp3_slow = mp3_frame_ms > 200;
}
#endif
