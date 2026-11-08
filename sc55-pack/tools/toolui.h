/* toolui.h - the common screen of the pack's small test programs (JOYTEST, KMTEST, MPUTEST, USBTEST,
 * TIMETEST), so they look like MENU, MIDPLAY and GAMETEST: a header band, raised panels, a hint line and a
 * key bar, in the style, font and display mode chosen in MENU (THEME.CFG; Shift at start = safe mode).
 * Header-only, built on tui.h. Live screens call tu_poll() in their loop instead of tui_key(). */
#ifndef TOOLUI_INCLUDED
#define TOOLUI_INCLUDED
#include <unistd.h>
#include <ctype.h>
#include "tui.h"

static char tu_home[260];
static int tu_active;

/* the folder of the program (MENU's home: THEME.CFG lives there) */
TUI_FN void tu_find_home(const char* argv0)
{
    char* s;
    snprintf(tu_home, sizeof tu_home, "%s", argv0 ? argv0 : "");
    for (s = tu_home; *s; ++s) if (*s == '/') *s = '\\';
    s = strrchr(tu_home, '\\');
    if (s) *s = 0; else if (!getcwd(tu_home, sizeof tu_home)) strcpy(tu_home, ".");
}
TUI_FN void tu_start(const char* argv0)
{
    tu_find_home(argv0);
    tui_theme_load(tu_home);
    tui_init();
    tu_active = 1;
}
TUI_FN void tu_end(void) { if (tu_active) { tui_done(); tu_active = 0; } }

/* header band: program name, what it does, the style on the right (like GAMETEST) */
TUI_FN void tu_head(const char* name, const char* what, const char* right)
{
    int x;
    tui_clear(0x10);
    tui_fill(1, 1, 80, 1, 0x30, ' ');
    if (!right) right = tui_theme_name[tui_theme];
    x = 2 + tui_put(2, 1, 0x3F, name);
    {
        char b[100]; snprintf(b, sizeof b, "\xB3 %s", what);
        tui_putw(x + 1, 1, 80 - (int)strlen(right) - x - 2, 0x30, b);
    }
    tui_put(80 - (int)strlen(right), 1, 0x30, right);
}
/* hint line (row 24) and key bar (row 25) */
TUI_FN void tu_foot(const char* hint, const char* keys)
{
    tui_fill(1, 24, 80, 1, 0x10, ' ');
    if (hint) tui_putw(2, 24, 78, 0x1E, hint);
    tui_fill(1, 25, 80, 1, 0x30, ' ');
    if (keys) tui_putw(2, 25, 78, 0x30, keys);
}
/* a lamp: label in a coloured cell, lit (lime) or dark */
TUI_FN int tu_lamp(int x, int y, int on, const char* label)
{
    char b[40];
    snprintf(b, sizeof b, " %s ", label);
    return tui_put(x, y, on ? 0xA0 : 0x87, b);
}
/* a lamp coloured for good (lime) / bad (pink) / unknown (slate) */
TUI_FN int tu_state(int x, int y, int state, const char* label)
{
    char b[60];
    snprintf(b, sizeof b, " %s ", label);
    return tui_put(x, y, state > 0 ? 0xA0 : state < 0 ? 0xCF : 0x87, b);
}
/* a 2D position box (stick, mouse): w x h cells with a centre cross, a dot at fx, fy (0..1); fx < 0 = nothing */
TUI_FN void tu_plot(int x, int y, int w, int h, float fx, float fy, int dot_attr)
{
    int i, j, cx = x + w / 2, cy = y + h / 2;
    for (j = 0; j < h; ++j) for (i = 0; i < w; ++i)
    {
        int ch = 0xFA;
        if (x + i == cx && y + j == cy) ch = 0xC5;
        else if (x + i == cx) ch = 0xB3;
        else if (y + j == cy) ch = 0xC4;
        tui_cell(x + i, y + j, 0x08, ch);
    }
    if (fx < 0) { tui_put(x + (w - 13) / 2, cy, 0x08, " not present "); return; }
    if (fx > 1) fx = 1;
    if (fy > 1) fy = 1;
    if (fy < 0) fy = 0;
    tui_cell(x + (int)(fx * (w - 1) + 0.5f), y + (int)(fy * (h - 1) + 0.5f), dot_attr, 0xFE);
}
/* a log area: the last lines of a text (newline-separated) in a w x h area */
TUI_FN void tu_log(int x, int y, int w, int h, int attr, const char* text)
{
    const char* lines[400];
    int n = 0, i;
    const char* s = text;
    while (*s && n < 400)
    {
        const char* e = strchr(s, '\n');
        int len = e ? (int)(e - s) : (int)strlen(s);
        /* long lines are cut into pieces of w */
        while (len > w && n < 400) { lines[n++] = s; s += w; len -= w; }
        if (n < 400) lines[n++] = s;
        s = e ? e + 1 : s + len;
    }
    for (i = 0; i < h; ++i)
    {
        int k = n - h + i;
        char b[200]; int len = 0;
        if (k >= 0)
        {
            const char* e = strchr(lines[k], '\n');
            len = e ? (int)(e - lines[k]) : (int)strlen(lines[k]);
            if (len > w) len = w;
            memcpy(b, lines[k], len);
        }
        b[len] = 0;
        tui_putw(x, y + i, w, attr, b);
    }
}
/* word-wrapped text that may contain newlines; returns the lines used */
TUI_FN int tu_text(int x, int y, int w, int h, int attr, const char* s)
{
    int used = 0;
    while (*s && used < h)
    {
        char b[400]; const char* e = strchr(s, '\n');
        int n = e ? (int)(e - s) : (int)strlen(s);
        if (n > (int)sizeof b - 1) n = sizeof b - 1;
        memcpy(b, s, n); b[n] = 0;
        used += b[0] ? tui_wrap(x, y + used, w, h - used, attr, b) : 1;
        s = e ? e + 1 : s + n;
    }
    return used;
}
/* draws the frame; returns a key (as tui_key) or 0. For live screens: call ~30 times a second. */
TUI_FN int tu_poll(void)
{
    int c;
    tui_flush();
    if (tui_fade_pending) { tui_fade_pending = 0; tui_fade(1); }
    if (!kbhit()) return 0;
    c = getch();
    if (c == 0 || c == 0xE0) c = 0x100 | getch();
    if (c == 0x13E && tu_home[0]) { tui_pick_mode(tu_home); tui_invalidate(); return 0; }   /* F4: display mode */
    return c;
}
/* a centred message window over the current screen, until a key */
TUI_FN int tu_wait_key(void)
{
    int c;
    while ((c = tu_poll()) == 0) __dpmi_yield();
    return c;
}
#endif
