/*
 * 86Box    Emulation of a wired Xbox 360 controller (045E:028E) on USB,
 *          for testing DOS USB host code. Local test addition.
 *
 *          Pad state comes from a text file that is read again whenever it
 *          changes: "lx=.. ly=.. rx=.. ry=.. lt=.. rt=.. buttons=HEX"
 *          (sticks -32768..32767, triggers 0..255, buttons as the report's
 *          16-bit word). Pad 1 reads $XPAD_STATE (default xpad1.st in the
 *          current directory), pad 2 $XPAD_STATE2 (xpad2.st). $XPAD sets how
 *          many pads are plugged in at hard reset (0-2, default 0).
 *          Slots "usb_xpad" and "usb_xpad2" can be hot-plugged with the
 *          automation command "usb plug|unplug <slot name>".
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/mem.h>
#include <86box/usb.h>
#include <86box/usb_dev.h>
#include <86box/plat_unused.h>

static const uint8_t xpad_dev_desc[18] = {
    18, 0x01, 0x00, 0x02, 0xff, 0xff, 0xff, 8,
    0x5e, 0x04, 0x8e, 0x02, 0x14, 0x01, 1, 2, 3, 1
};

#define XPAD_CFG_LEN (9 + 9 + 17 + 7 + 7 + 9 + 7 + 7)

static const uint8_t xpad_cfg_desc[XPAD_CFG_LEN] = {
    9, 0x02, XPAD_CFG_LEN, 0x00, 2, 1, 0, 0xa0, 0xfa,
    /* Interface 0: the controls, FF/5D/01 */
    9, 0x04, 0, 0, 2, 0xff, 0x5d, 0x01, 0,
    17, 0x21, 0x00, 0x01, 0x01, 0x25, 0x81, 0x14, 0x00, 0x00, 0x00, 0x00, 0x13, 0x01, 0x08, 0x00, 0x00,
    7, 0x05, 0x81, 0x03, 0x20, 0x00, 4,
    7, 0x05, 0x01, 0x03, 0x20, 0x00, 8,
    /* Interface 1: headset port (unused) */
    9, 0x04, 1, 0, 2, 0xff, 0x5d, 0x03, 0,
    7, 0x05, 0x82, 0x03, 0x20, 0x00, 2,
    7, 0x05, 0x02, 0x03, 0x20, 0x00, 4
};

static const char *const xpad_strings[] = { "Microsoft", "Controller", "08B3C5A" };

typedef struct xpad_t {
    usb_dev_t usb;
    int       index;
    char      path[1024];
    time_t    mtime;
    long      mtime_ns;
    off_t     size;
    int16_t   lx, ly, rx, ry;
    uint8_t   lt, rt;
    uint16_t  buttons;
    uint8_t   sent[20];
    int       have_sent;
} xpad_t;

static void
xpad_read_state(xpad_t *dev)
{
    struct stat st;
    FILE       *f;
    char        buf[512];
    char       *tok;

    if (stat(dev->path, &st) != 0)
        return;
    if ((st.st_mtime == dev->mtime) && (st.st_mtim.tv_nsec == dev->mtime_ns) && (st.st_size == dev->size))
        return;
    dev->mtime    = st.st_mtime;
    dev->mtime_ns = st.st_mtim.tv_nsec;
    dev->size     = st.st_size;
    if ((f = fopen(dev->path, "r")) == NULL)
        return;
    buf[fread(buf, 1, sizeof(buf) - 1, f)] = 0;
    fclose(f);
    dev->lx = dev->ly = dev->rx = dev->ry = 0;
    dev->lt = dev->rt = 0;
    dev->buttons = 0;
    for (tok = strtok(buf, " \t\r\n"); tok != NULL; tok = strtok(NULL, " \t\r\n")) {
        char *eq = strchr(tok, '=');
        long  v;
        if (eq == NULL)
            continue;
        *eq = 0;
        v   = strtol(eq + 1, NULL, strcmp(tok, "buttons") ? 10 : 16);
        if (v > 32767 && strcmp(tok, "buttons"))
            v = 32767;
        if (!strcmp(tok, "lx")) dev->lx = (int16_t) v;
        else if (!strcmp(tok, "ly")) dev->ly = (int16_t) v;
        else if (!strcmp(tok, "rx")) dev->rx = (int16_t) v;
        else if (!strcmp(tok, "ry")) dev->ry = (int16_t) v;
        else if (!strcmp(tok, "lt")) dev->lt = (uint8_t) v;
        else if (!strcmp(tok, "rt")) dev->rt = (uint8_t) v;
        else if (!strcmp(tok, "buttons")) dev->buttons = (uint16_t) v;
    }
}

static int
xpad_control(UNUSED(usb_dev_t *usb), const uint8_t *setup, UNUSED(uint8_t *data))
{
    /* Vendor and class requests: the 360 pad answers a few vendor reads
       (serial etc.); a DOS driver needs none of them. HID class SET_IDLE
       and similar are accepted and ignored. */
    if ((setup[0] & 0x80) == 0)
        return 0;
    return USB_DATA_STALL;
}

static int
xpad_data_in(usb_dev_t *usb, int ep, uint8_t *buf, UNUSED(int maxlen))
{
    xpad_t *dev = (xpad_t *) usb->priv;
    uint8_t r[20];

    if (ep != 1)
        return USB_DATA_NAK;
    xpad_read_state(dev);
    memset(r, 0, sizeof(r));
    r[0]  = 0x00;
    r[1]  = 0x14;
    r[2]  = dev->buttons & 0xff;
    r[3]  = dev->buttons >> 8;
    r[4]  = dev->lt;
    r[5]  = dev->rt;
    r[6]  = dev->lx & 0xff; r[7]  = (uint16_t) dev->lx >> 8;
    r[8]  = dev->ly & 0xff; r[9]  = (uint16_t) dev->ly >> 8;
    r[10] = dev->rx & 0xff; r[11] = (uint16_t) dev->rx >> 8;
    r[12] = dev->ry & 0xff; r[13] = (uint16_t) dev->ry >> 8;
    /* A real pad sends a report only when something changed. */
    if (dev->have_sent && !memcmp(r, dev->sent, sizeof(r)))
        return USB_DATA_NAK;
    memcpy(dev->sent, r, sizeof(r));
    dev->have_sent = 1;
    memcpy(buf, r, sizeof(r));
    return sizeof(r);
}

static int
xpad_data_out(UNUSED(usb_dev_t *usb), UNUSED(int ep), UNUSED(const uint8_t *buf), UNUSED(int len))
{
    return 0; /* LED / rumble commands */
}

static void
xpad_reset(usb_dev_t *usb)
{
    xpad_t *dev = (xpad_t *) usb->priv;

    dev->have_sent = 0;
}

static const usb_dev_ops_t xpad_ops = {
    .control    = xpad_control,
    .data_in    = xpad_data_in,
    .data_out   = xpad_data_out,
    .reset      = xpad_reset,
    .configured = NULL
};

static void *
xpad_init(const device_t *info)
{
    xpad_t     *dev = (xpad_t *) calloc(1, sizeof(xpad_t));
    const char *p;

    dev->index = info->local;
    p          = getenv(dev->index ? "XPAD_STATE2" : "XPAD_STATE");
    snprintf(dev->path, sizeof(dev->path), "%s", p ? p : (dev->index ? "xpad2.st" : "xpad1.st"));
    dev->mtime = -1;

    dev->usb.name      = dev->index ? "Xbox 360 Controller 2" : "Xbox 360 Controller";
    dev->usb.low_speed = 0;
    dev->usb.dev_desc  = xpad_dev_desc;
    dev->usb.cfg_desc  = xpad_cfg_desc;
    dev->usb.cfg_len   = XPAD_CFG_LEN;
    dev->usb.strings   = xpad_strings;
    dev->usb.n_strings = 3;
    dev->usb.ops       = &xpad_ops;
    dev->usb.priv      = dev;
    usb_dev_init(&dev->usb);
    xpad_reset(&dev->usb);
    usb_dev_add(&dev->usb, info);

    return dev;
}

static void
xpad_close(void *priv)
{
    xpad_t *dev = (xpad_t *) priv;

    usb_dev_remove(&dev->usb);
    free(dev);
}

const device_t usb_xpad_device = {
    .name = "Xbox 360 Controller", .internal_name = "usb_xpad", .flags = DEVICE_USB, .local = 0,
    .init = xpad_init, .close = xpad_close
};

const device_t usb_xpad2_device = {
    .name = "Xbox 360 Controller 2", .internal_name = "usb_xpad2", .flags = DEVICE_USB, .local = 1,
    .init = xpad_init, .close = xpad_close
};

/* Hard reset: plug in $XPAD pads (after the hub, so they may go behind it). */
void
usb_xpad_reset(void)
{
    const char *p = getenv("XPAD");
    int         n = p ? atoi(p) : 0;

    if (n >= 1)
        device_add(&usb_xpad_device);
    if (n >= 2)
        device_add(&usb_xpad2_device);
}
