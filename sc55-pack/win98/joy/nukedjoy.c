/*
 * nukedjoy.c - NUKEDJOY.VXD poll routines (called by VJoyD at ring 0, __stdcall).
 * The joystick state is the helper's (SC55W.EXE) SC55W_SHARED.wjoy[], reached through
 * SC55W.VXD's DDB (Reserved1 = block address, Reserved2 = SC55W_DDB_MAGIC while attached).
 * No SC55W running = "unplugged" (JOY_OEMPOLLRC_FAIL), so games see no joystick.
 * The first Windows joystick id with this type gets NUKED joystick 1, the next one 2.
 */
#include "../common/sc55wif.h"

typedef struct { u32 x, y, z, r, u, v; } JOYPOS;
typedef struct {
    u32 id, do_other;
    JOYPOS jp;
    u32 pov, buttons, button_number;
} JOYOEMPOLLDATA;
typedef struct { u32 max_buttons, max_axes, num_axes; } JOYOEMHWCAPS;

#define POLL_GETBUTTONS 6
#define RC_OK   1
#define RC_FAIL 0
#define MAXJOY 16

extern u32 __cdecl vxd_get_ddb(u32 id, const char *name8);

static u8 in_use[MAXJOY];

static SC55W_SHARED *block(void)
{
    u32 *d = (u32 *)vxd_get_ddb(0, "SC55W   ");
    if (!d || d[18] != SC55W_DDB_MAGIC || !d[17]) return 0;
    return (SC55W_SHARED *)d[17];
}

static int slot_of(u32 id)
{
    u32 i;
    int n = 0;
    for (i = 0; i < id && i < MAXJOY; i++) if (in_use[i]) n++;
    return n < SC55W_NWJOY ? n : SC55W_NWJOY - 1;
}

int __stdcall nj_poll(int type, JOYOEMPOLLDATA *d)
{
    SC55W_SHARED *s = block();
    SC55W_WJOY *w;
    u32 b;
    int n;
    if (!s) return RC_FAIL;
    w = &s->wjoy[slot_of(d->id)];
    if (!w->present) return RC_FAIL;
    w->polls++;
    w->joyid = d->id + 1;
    b = w->buttons;
    d->buttons = b;
    for (n = 0; n < 32 && !(b >> n & 1); n++) ;
    d->button_number = n < 32 ? n + 1 : 0;
    if (type == POLL_GETBUTTONS) return RC_OK;
    d->pov = w->pov == 0xFFFF ? 0xFFFFUL : w->pov;
    if (type == 0) {                        /* one axis, do_other = which, value in x */
        d->jp.x = d->do_other < 6 ? w->axis[d->do_other] : 32768UL;
        return RC_OK;
    }
    d->jp.x = w->axis[0]; d->jp.y = w->axis[1]; d->jp.z = w->axis[2];
    d->jp.r = w->axis[3]; d->jp.u = w->axis[4]; d->jp.v = w->axis[5];
    return RC_OK;
}

int __stdcall nj_hwcaps(int id, JOYOEMHWCAPS *c)
{
    (void)id;
    c->max_buttons = 16;
    c->max_axes = 6;
    c->num_axes = 6;
    return 1;
}

int __stdcall nj_joyid(int id, int inuse)
{
    if (id >= 0 && id < MAXJOY) in_use[id] = (u8)(inuse != 0);
    return 1;
}
