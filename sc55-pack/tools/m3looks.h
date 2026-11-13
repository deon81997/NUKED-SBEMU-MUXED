/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* m3looks.h - what MIDPLAY's 3D view and the 3D menu share: the 11 colour themes (C in MIDPLAY, F6 in the menu),
 * the vector look's smooth Wu lines (additive, clipped to the drawn area m3_rw x m3_rh) and the voxel look's blocks.
 * Include after m3core.h. */
#ifndef M3LOOKS_INCLUDED
#define M3LOOKS_INCLUDED
typedef struct { float h0, hs, sat; uint32_t s0, s1, grid, body, face, l0, l1, ink, acc; } M3THEME;
static const char* const m3_theme_names[11] = { "classic", "neon", "amber CRT", "green phosphor", "sunset", "ice", "synthwave",
    "SC-55 style", "SC-88 style", "MT-32 style", "CM-32L style" };
/* hue start, hue span, saturation, sky top, sky bottom, grid; then the module: body, face, LCD top / bottom, LCD ink,
 * accent. Theme 0 = the program's own colours (the text style's). The hardware styles copy the colours of the Roland
 * boxes this pack emulates (no logos, NUKED stays) */
static const M3THEME m3_themes[11] = {
        { 0, 0, 0, 0, 0, 0,                                 0x26262C, 0x28282E, 0xA8E048, 0x7CB830, 0x10200A, 0xFFA030 },
        { 200, 140, 0.62f, 0x04020E, 0x140A26, 0x5A3A7A,       0x18101E, 0x1C1424, 0x60F0FF, 0x30A0E0, 0x081830, 0xFF30D0 },
        { 22, 22, 0.85f, 0x0A0500, 0x221000, 0x8A5A16,      0x1A140C, 0x201810, 0xFFB040, 0xD08020, 0x2A1404, 0xFFC060 },
        { 100, 40, 0.75f, 0x000802, 0x002006, 0x1E8A36,      0x0E1610, 0x121A14, 0x60FF80, 0x30C050, 0x042008, 0x60FF80 },
        { 300, 110, 0.7f, 0x140626, 0x6A2A30, 0x9A5030,    0x2A1820, 0x302028, 0xFFC080, 0xE08050, 0x301008, 0xFF6040 },
        { 170, 70, 0.45f, 0x08122A, 0x2C4C6C, 0x6A90AA,     0x506070, 0x5A6A7A, 0xD0F0FF, 0x90C0E0, 0x102030, 0x40C0FF },
        { 285, 95, 0.6f, 0x07020F, 0x220A26, 0x3A6A78,     0x1A1028, 0x201430, 0xFF60D0, 0xB030A0, 0x200818, 0x20F0FF },
        { 55, 85, 0.6f, 0x08090A, 0x1A1D1C, 0x7A6A3A,       0x2A2A2E, 0x2E2E33, 0xB0E050, 0x80BC34, 0x0E1C08, 0xFF7A20 },
        { 22, 34, 0.7f, 0x070605, 0x1C1610, 0x7A5A2C,       0x1A1A1E, 0x1E1E22, 0xFFB848, 0xD88A24, 0x241204, 0xFFC860 },
        { 8, 46, 0.62f, 0x0A0808, 0x221614, 0x6E3A2C,       0x303034, 0x36363A, 0xFF7A3A, 0xC8481C, 0x2A0A04, 0xE03030 },
        { 95, 110, 0.5f, 0x16181C, 0x363A42, 0x4A5260,      0xC8C2B4, 0xD0CABC, 0x98D278, 0x64A84C, 0x10240A, 0x404448 } };
static uint32_t m3_hsv(float h, float sat, float v)
{
    float r, g, b, f, p, q, t; int i;
    h = fmodf(h, 360.0f); if (h < 0) h += 360.0f; h /= 60.0f; i = (int)h; f = h - i;
    p = v * (1 - sat); q = v * (1 - sat * f); t = v * (1 - sat * (1 - f));
    switch (i) { case 0: r = v; g = t; b = p; break; case 1: r = q; g = v; b = p; break; case 2: r = p; g = v; b = t; break;
                 case 3: r = p; g = q; b = v; break; case 4: r = t; g = p; b = v; break; default: r = v; g = p; b = q; }
    return (uint32_t)(r * 255) << 16 | (uint32_t)(g * 255) << 8 | (uint32_t)(b * 255);
}
static void m3_addpx(int x, int y, uint32_t c, int a)
{
    uint32_t* p; int r, g, b;
    if ((unsigned)x >= (unsigned)m3_rw || (unsigned)y >= (unsigned)m3_rh || a <= 0) return;
    p = &m3_fb[y * M3W + x];
    r = (*p >> 16 & 255) + ((c >> 16 & 255) * a >> 8); g = (*p >> 8 & 255) + ((c >> 8 & 255) * a >> 8); b = (*p & 255) + ((c & 255) * a >> 8);
    *p = (uint32_t)(r > 255 ? 255 : r) << 16 | (uint32_t)(g > 255 ? 255 : g) << 8 | (uint32_t)(b > 255 ? 255 : b);
}
static void m3_wline(float x0, float y0, float x1, float y1, uint32_t c)
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
        if (steep) { m3_addpx(yi, x, c, 256 - a); m3_addpx(yi + 1, x, c, a); }
        else { m3_addpx(x, yi, c, 256 - a); m3_addpx(x, yi + 1, c, a); }
    }
}
static void m3_line3(M3V a, M3V b, uint32_t c)
{
    M3P p = m3_xf(a, 0, 0, 1), q = m3_xf(b, 0, 0, 1); float f = m3_f * m3_rw / M3_FBASE, nz = 0.1f;
    if (p.z < nz && q.z < nz) return;
    if (p.z < nz) { float t = (nz - p.z) / (q.z - p.z); p.x += (q.x - p.x) * t; p.y += (q.y - p.y) * t; p.z = nz; }
    if (q.z < nz) { float t = (nz - q.z) / (p.z - q.z); q.x += (p.x - q.x) * t; q.y += (p.y - q.y) * t; q.z = nz; }
    m3_wline(m3_rw / 2 + p.x * f / p.z, m3_rh / 2 - p.y * f * m3_fyk / p.z, m3_rw / 2 + q.x * f / q.z, m3_rh / 2 - q.y * f * m3_fyk / q.z, c);
}
static void m3_wbox(float x, float y, float z, float hx, float hy, float hz, float yaw, uint32_t c)
{
    static const int e[12][2] = { {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7} };
    static const int k[8][3] = { {-1,1,-1},{1,1,-1},{1,-1,-1},{-1,-1,-1},{-1,1,1},{1,1,1},{1,-1,1},{-1,-1,1} };
    M3V v[8]; int i; float cy = cosf(yaw), sy = sinf(yaw);
    for (i = 0; i < 8; i++)
    {
        float px = k[i][0] * hx, py = k[i][1] * hy, pz = k[i][2] * hz;
        v[i] = m3_v(x + px * cy + pz * sy, y + py, z - px * sy + pz * cy);
    }
    for (i = 0; i < 12; i++) m3_line3(v[e[i][0]], v[e[i][1]], c);
}
static void m3_wcircle(float x, float y, float z, float r, float rot, uint32_t c)
{
    int i; M3V p0 = m3_v(x + r, y, z);
    for (i = 1; i <= 24; i++)
    {
        float a = i * 6.2831853f / 24; M3V p1 = m3_v(x + r * cosf(a), y + r * sinf(a), z);
        m3_line3(p0, p1, c); p0 = p1;
    }
    m3_line3(m3_v(x, y, z), m3_v(x + r * cosf(rot), y + r * sinf(rot), z), c);
}

static void m3_cube(float x, float y, float z, float h, uint32_t c, float glow) { m3_box(m3_v(x, y, z), h, h, h, 0, NULL, c, glow, 256); }
#endif
