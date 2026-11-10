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
static const uint32_t mp3_chcol[16] = {
    0x40A0FF, 0x40E0A0, 0xA0E040, 0xFFD040, 0xFF9040, 0xFF5060, 0xE060C0, 0xA070FF,
    0x60C0FF, 0xFFFFFF, 0x50FFD0, 0xC0FF60, 0xFFB0A0, 0xFF80E0, 0xB0A0FF, 0x80D0D0 };

/* buffers and textures, made before the music starts (switching to 3D later then costs only the mode set) */
static void mp3_prepare(void)
{
    if (!m3_fb) m3_fb = (uint32_t*)malloc(M3W * M3H * 4);
    if (!m3_zb) m3_zb = (float*)malloc(M3W * M3H * sizeof(float));
    if (!m3_fb || !m3_zb) return;
    m3_scr.w = M3W; m3_scr.h = M3H; m3_scr.p = m3_fb;
    m3_theme_sync();
    if (!mp3_panel.p) mp3_panel = m3_surf(512, 128);
    if (!mp3_keys.p) mp3_keys = m3_surf(512, 64);
    if (m3_tsc_us == 0) m3_time_init();
}
static void mp3_make_panel(const char* title, double frac, int paused);
static int mp3_enter(void)
{
    if (mp3_on) return 1;
    if (!m3_fb) m3_fb = (uint32_t*)malloc(M3W * M3H * 4);
    if (!m3_zb) m3_zb = (float*)malloc(M3W * M3H * sizeof(float));
    if (!m3_fb || !m3_zb) return 0;
    m3_scr.w = M3W; m3_scr.h = M3H; m3_scr.p = m3_fb;
    m3_theme_sync();
    if (!mp3_panel.p) mp3_panel = m3_surf(512, 128);
    if (!mp3_keys.p) mp3_keys = m3_surf(512, 64);
    if (!m3_vbe_open()) return 0;
    if (m3_tsc_us == 0) m3_time_init();
    m3_half = 0; m3_rw = M3W; m3_rh = M3H; mp3_slow = 0;
    mp3_on = 1; mp3_last_frame = 0;
    return 1;
}
static void mp3_leave(void)
{
    if (!mp3_on) return;
    m3_vbe_close();
    mp3_on = 0;
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
    m3_rect(&m3_scr, 0, M3H - 46, M3W, 46, 0x000000, 160);
    fmt_time(t1, (unsigned long)(total_us / 1000000ULL)); fmt_time(t2, el);
    m3_text(&m3_scr, 10, M3H - 42, 0, t2, 0xFFFFFF, 256);
    m3_text(&m3_scr, M3W - 58, M3H - 42, 0, t1, 0xFFFFFF, 256);
    w = M3W - 140; n = total_us ? (int)((el_us > total_us ? total_us : el_us) * (uint64_t)w / total_us) : 0;
    m3_rect(&m3_scr, 66, M3H - 38, w, 8, 0x303040, 256);
    m3_vgrad(&m3_scr, 66, M3H - 38, n, 8, 0x60FFA0, 0x20A060, 256);
    if (paused) m3_text_sh(&m3_scr, M3W / 2 - 28, M3H - 64, 0, "PAUSED", 0xFFC040);
    else if (done) m3_text_sh(&m3_scr, M3W / 2 - 20, M3H - 64, 0, "DONE", 0x60FF60);
    if (mp3_slow) m3_text_sh(&m3_scr, 8, 28, 0, "Few frames on this PC: F4 half resolution (faster), S text views.", 0xFFC040);
    m3_text(&m3_scr, 10, M3H - 22, 0, "SPACE pause  N/P song  <> seek  TAB lists  S view  F4 resolution  F1 help  ESC quit", 0xA0A0B8, 256);
}

static void mp3_draw(const Song* s, const char* name, uint64_t el_us, uint64_t total_us, int paused, int done)
{
    double t = m3_now(), t0; float W = 2.6f, H = 0.62f, D = 1.2f, dt; int c;
    double frac = total_us ? (double)(el_us > total_us ? total_us : el_us) / (double)total_us : 0;
    if (!mp3_on) return;
    if (t - mp3_last_frame < 0.016) return;
    m3_theme_sync();                          /* F3 (style) re-colours the scene too */   /* up to 60 a second (the screen's refresh); frames follow each other (the music is sent from inside) */
    dt = mp3_last_frame ? (float)(t - mp3_last_frame) : 0.06f; if (dt > 0.2f) dt = 0.2f;
    mp3_last_frame = t; t0 = t;
    lcd_query();
    m3_bgphase = (float)t;
    m3_fog = m3_fogc; m3_fog0 = 8; m3_fog1 = 24;
    m3_camera(0.6f * sinf((float)t * 0.2f), 0.55f + 0.08f * sinf((float)t * 0.13f), -6.6f, -0.1f * sinf((float)t * 0.2f), -0.16f);
    m3_sky();
    M3_HOOK();
    m3_draw_floor(-H - 0.02f, 256);
    M3_HOOK();
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
    mp3_bar(base_of(name), s->title, el_us, total_us, paused, done);
    M3_HOOK();
    m3_present();
    /* frame time: half resolution when slow; a note when even that is too slow */
    mp3_frame_ms = (m3_now() - t0) * 1000;
    st_frame((uint64_t)(mp3_frame_ms * 1000));
    /* always full resolution (sharp): the music no longer waits for frames, so a slow PC only gets fewer of
     * them (F4 still switches to half resolution by hand). A note when it is down to ~2 frames a second. */
    mp3_slow = mp3_frame_ms > 200;
}
#endif
