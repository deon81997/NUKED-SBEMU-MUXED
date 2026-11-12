/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/*
 * nukedjoy.c - NUKEDJOY.VXD poll routines (called by VJoyD at ring 0, __stdcall).
 * The joystick state is the helper's (SC55W.EXE) SC55W_SHARED.wjoy[], reached through
 * SC55W.VXD's DDB (Reserved1 = block address, Reserved2 = SC55W_DDB_MAGIC while attached).
 * No SC55W running = "unplugged" (JOY_OEMPOLLRC_FAIL), so games see no joystick.
 * The first Windows joystick id with this type gets NUKED joystick 1, the next one 2.
 */
#include "../common/sc55wif.h"
#ifndef NJ_BUILD
#define NJ_BUILD 0
#endif
#ifndef NJ_IFACE
#define NJ_IFACE 0
#endif

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
extern void __cdecl vxd_note(const char *name, u32 value);
extern u32  __cdecl vxd_vjoyd_version(void);
extern void __cdecl vxd_vjoyd_register_dx5(void *info);
extern void __cdecl vxd_vjoyd_register_old(void *poll, void *cfg, void *hwcaps, void *joyid);
static int last_rc = -2;

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

static int poll_rc(int rc, u32 id)
{
    if (rc != last_rc) { last_rc = rc; vxd_note("Poll", (u32)rc | (id << 8)); }
    return rc;
}

int __stdcall nj_poll(int type, JOYOEMPOLLDATA *d)
{
    SC55W_SHARED *s = block();
    SC55W_WJOY *w;
    u32 b;
    int n;
    if (!s) return poll_rc(RC_FAIL, d->id);
    w = &s->wjoy[slot_of(d->id)];
    if (!w->present) { poll_rc(2, d->id); return RC_FAIL; }   /* noted as 2 */
    poll_rc(RC_OK, d->id);
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
    vxd_note("JoyId", (u32)id << 8 | (u32)(inuse != 0));
    return 1;
}

/* ---- DirectX 5 interface (VJoyD 5.0 and later: Windows 98) ---------------------------- */
typedef struct { u32 x, y, z, r, u, v, pov[4], btn[4], res[2]; } VJPOLLDATA;
typedef struct { u32 size, flags, unit_id, devnode; u32 hws_flags, hws_buttons; } DID_INITPARAMS;
typedef struct { u32 size; void *poll, *cfg, *init, *escape, *ctrlmsg; } VJPOLLREG;
typedef struct {
    u32 size, function, flags;
    const char *callout;
    u32 fw_rev, hw_rev, drv_ver;
    void *desc;
    VJPOLLREG *pollreg;
    void *forcereg;
    u32 reserved;
} VJREGDRVINFO;

#define VJRT_LOADED 1
#define VJDF_NODEVNODE          0x00010000UL
#define VJDF_ISANALOGPORTDRIVER 0x00020000UL
#define S_OK_        0UL
#define S_FALSE_     1UL
#define E_FAIL_      0x80004005UL
#define E_NOTIMPL_   0x80004001UL
#define JOYPD_X 0x1
#define JOYPD_Y 0x2
#define JOYPD_Z 0x4
#define JOYPD_R 0x8
#define JOYPD_U 0x10
#define JOYPD_V 0x20
#define JOYPD_POV0 0x40
#define JOYPD_BTN0 0x400
#define JOYPD_POSITION 0x10000

static u32 last_hr = 0xFFFFFFFFUL;
static u32 note_hr(u32 hr, u32 id)
{
    if (hr != last_hr) { last_hr = hr; vxd_note("Poll5", hr ^ (id << 24)); }
    return hr;
}

u32 __stdcall nj_poll5(u32 id, u32 *mask, VJPOLLDATA *d)
{
    SC55W_SHARED *s = block();
    SC55W_WJOY *w;
    u32 want = *mask, got = 0;
    if (!s) { *mask = 0; return note_hr(E_FAIL_, id); }
    w = &s->wjoy[slot_of(id)];
    if (!w->present) { *mask = 0; return note_hr(E_FAIL_ | 2, id) & ~2UL; }
    w->polls++;
    w->joyid = id + 1;
    /* everything, whatever was asked (VJoyD reads fields it did not ask for) */
    d->x = w->axis[0]; d->y = w->axis[1]; d->z = w->axis[2];
    d->r = w->axis[3]; d->u = w->axis[4]; d->v = w->axis[5];
    d->pov[0] = w->pov == 0xFFFF ? 0xFFFFFFFFUL : w->pov;
    d->pov[1] = d->pov[2] = d->pov[3] = 0xFFFFFFFFUL;
    d->btn[0] = w->buttons; d->btn[1] = d->btn[2] = d->btn[3] = 0;
    got = want & (JOYPD_X | JOYPD_Y | JOYPD_Z | JOYPD_R | JOYPD_U | JOYPD_V | JOYPD_POV0 | JOYPD_BTN0);
    *mask = got | (want & JOYPD_POSITION);
    return note_hr(S_OK_, id);
}

u32 __stdcall nj_init5(u32 id, DID_INITPARAMS *p)
{
    if (id < MAXJOY) in_use[id] = 1;
    vxd_note("Init5", id | (p ? p->flags << 8 : 0));
    return S_OK_;
}

u32 __stdcall nj_escape5(u32 id, u32 effect, void *esc) { (void)id; (void)effect; (void)esc; return E_NOTIMPL_; }
u32 __stdcall nj_ctrlmsg5(u32 id, u32 msg, u32 param) { (void)id; (void)param; vxd_note("CtrlMsg", msg); return S_FALSE_; }

static VJPOLLREG pollreg;
static VJREGDRVINFO reginfo;

/* called from NJ_Control on Sys_Dynamic_Device_Init and on VJoyD's re-init control message;
   returns 5 for the DX5 interface, 1 for the original one */
static u32 n_reg;
u32 __cdecl nj_register(u32 msg)
{
    u32 v = vxd_vjoyd_version();
    n_reg++;
    vxd_note(msg == 0x1B ? "RegByInit" : "RegByReinit", n_reg);
    vxd_note("VmmVersion", v);
    vxd_note("Build", NJ_BUILD);
    if (NJ_IFACE == 1) v = 0;             /* test: force the original interface */
    if (v >= 0x40A) {          /* Windows 98 / ME: VJoyD with the DirectX 5 interface */
        pollreg.size = sizeof pollreg;
        pollreg.poll = (void *)nj_poll5;
        pollreg.cfg = 0;
        pollreg.init = (void *)nj_init5;
        pollreg.escape = (void *)nj_escape5;
        pollreg.ctrlmsg = (void *)nj_ctrlmsg5;
        reginfo.size = sizeof reginfo;
        reginfo.function = VJRT_LOADED;
        reginfo.flags = 0;
        reginfo.callout = "NUKEDJOY.VXD";
        reginfo.drv_ver = 0x10000;
        reginfo.pollreg = &pollreg;
        vxd_vjoyd_register_dx5(&reginfo);
        vxd_note("Init", 5);
        return 5;
    }
    vxd_vjoyd_register_old((void *)nj_poll, 0, (void *)nj_hwcaps, (void *)nj_joyid);
    vxd_note("Init", 1);
    return 1;
}
