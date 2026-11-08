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
 *          $XPAD_KIND / $XPAD_KIND2 = "one": an Xbox One pad instead (GIP,
 *          045E:02EA; silent until it gets the power-on packet, the Xbox
 *          button in its own packet that asks for an ack), "hid": a generic
 *          HID gamepad (0079:0006 style: X Y Z Z Rz bytes, hat, 12 buttons).
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

/* Xbox One (GIP) */
static const uint8_t one_dev_desc[18] = {
    18, 0x01, 0x00, 0x02, 0xff, 0x47, 0xd0, 64,
    0x5e, 0x04, 0xea, 0x02, 0x08, 0x04, 1, 2, 3, 1
};
#define ONE_CFG_LEN (9 + 9 + 7 + 7)
static const uint8_t one_cfg_desc[ONE_CFG_LEN] = {
    9, 0x02, ONE_CFG_LEN, 0x00, 1, 1, 0, 0xa0, 0xfa,
    9, 0x04, 0, 0, 2, 0xff, 0x47, 0xd0, 0,
    7, 0x05, 0x81, 0x03, 0x40, 0x00, 4,
    7, 0x05, 0x01, 0x03, 0x40, 0x00, 4
};

/* generic HID gamepad */
static const uint8_t hid_report_desc[] = {
    0x05, 0x01, 0x09, 0x04, 0xa1, 0x01,
    0xa1, 0x02,
    0x75, 0x08, 0x95, 0x05, 0x15, 0x00, 0x26, 0xff, 0x00, 0x35, 0x00, 0x46, 0xff, 0x00,
    0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x32, 0x09, 0x35, 0x81, 0x02,
    0x75, 0x04, 0x95, 0x01, 0x25, 0x07, 0x46, 0x3b, 0x01, 0x65, 0x14, 0x09, 0x39, 0x81, 0x42,
    0x65, 0x00, 0x75, 0x01, 0x95, 0x0c, 0x25, 0x01, 0x45, 0x01, 0x05, 0x09, 0x19, 0x01, 0x29, 0x0c, 0x81, 0x02,
    0x06, 0x00, 0xff, 0x75, 0x01, 0x95, 0x08, 0x25, 0x01, 0x45, 0x01, 0x09, 0x01, 0x81, 0x02,
    0xc0,
    0xc0
};
static const uint8_t hid_dev_desc[18] = {
    18, 0x01, 0x10, 0x01, 0x00, 0x00, 0x00, 8,
    0x79, 0x00, 0x06, 0x00, 0x07, 0x01, 1, 2, 0, 1
};
#define HID_CFG_LEN (9 + 9 + 9 + 7)
static const uint8_t hid_cfg_desc[HID_CFG_LEN] = {
    9, 0x02, HID_CFG_LEN, 0x00, 1, 1, 0, 0x80, 0xfa,
    9, 0x04, 0, 0, 1, 0x03, 0x00, 0x00, 0,
    9, 0x21, 0x10, 0x01, 0x21, 0x01, 0x22, sizeof(hid_report_desc), 0x00,
    7, 0x05, 0x81, 0x03, 0x08, 0x00, 10
};
static const char *const hid_strings[] = { "DragonRise Inc.", "Generic   USB  Joystick" };

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
    int       kind;                     /* 0 Xbox 360, 1 Xbox One, 2 HID gamepad */
    int       started, seq, guide_sent; /* Xbox One: power-on packet received; packet counter; Xbox button sent */
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
xpad_control(usb_dev_t *usb, const uint8_t *setup, uint8_t *data)
{
    xpad_t *dev = (xpad_t *) usb->priv;

    if ((dev->kind == 2) && (setup[0] == 0x81) && (setup[1] == 0x06) && (setup[3] == 0x22)) {
        memcpy(data, hid_report_desc, sizeof(hid_report_desc));     /* HID report descriptor */
        return sizeof(hid_report_desc);
    }
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
    if (dev->kind == 1) {
        uint8_t  g[18];
        uint16_t b = dev->buttons;
        int      lt = dev->lt * 4, rt = dev->rt * 4;

        if (!dev->started)
            return USB_DATA_NAK;
        if (((b & 0x0400) != 0) != dev->guide_sent) {               /* the Xbox button: its own packet */
            uint8_t p[6] = { 0x07, 0x30, 0, 0x02, 0, 0x5b };
            dev->guide_sent = (b & 0x0400) != 0;
            p[2] = (uint8_t) dev->seq++;
            p[4] = (uint8_t) dev->guide_sent;
            memcpy(buf, p, sizeof(p));
            return sizeof(p);
        }
        memset(g, 0, sizeof(g));
        g[0] = 0x20; g[1] = 0x00; g[3] = 0x0e;
        g[4] = ((b & 0x0010) ? 0x04 : 0) | ((b & 0x0020) ? 0x08 : 0) | ((b & 0x1000) ? 0x10 : 0) | ((b & 0x2000) ? 0x20 : 0) |
               ((b & 0x4000) ? 0x40 : 0) | ((b & 0x8000) ? 0x80 : 0);
        g[5] = (b & 0x000f) | ((b & 0x0100) ? 0x10 : 0) | ((b & 0x0200) ? 0x20 : 0) | ((b & 0x0040) ? 0x40 : 0) | ((b & 0x0080) ? 0x80 : 0);
        g[6] = lt & 0xff; g[7] = lt >> 8; g[8] = rt & 0xff; g[9] = rt >> 8;
        g[10] = dev->lx & 0xff; g[11] = (uint16_t) dev->lx >> 8; g[12] = dev->ly & 0xff; g[13] = (uint16_t) dev->ly >> 8;
        g[14] = dev->rx & 0xff; g[15] = (uint16_t) dev->rx >> 8; g[16] = dev->ry & 0xff; g[17] = (uint16_t) dev->ry >> 8;
        if (dev->have_sent && !memcmp(g + 4, dev->sent + 4, 14))
            return USB_DATA_NAK;
        g[2] = (uint8_t) dev->seq++;
        memcpy(dev->sent, g, sizeof(g));
        dev->have_sent = 1;
        memcpy(buf, g, sizeof(g));
        return sizeof(g);
    }
    if (dev->kind == 2) {
        uint8_t  h[8];
        uint16_t b = dev->buttons;
        static const int hat[16] = { 8, 0, 4, 8, 6, 7, 5, 8, 2, 1, 3, 8, 8, 8, 8, 8 };   /* index: R L D U bits */
        int u = b & 1, d = (b >> 1) & 1, l = (b >> 2) & 1, r = (b >> 3) & 1;
        h[0] = (uint8_t) ((dev->lx + 32768) >> 8);
        h[1] = (uint8_t) (255 - ((dev->ly + 32768) >> 8));
        h[2] = (uint8_t) ((dev->rx + 32768) >> 8);
        h[3] = h[2];
        h[4] = (uint8_t) (255 - ((dev->ry + 32768) >> 8));
        h[5] = (uint8_t) (hat[(r << 3) | (l << 2) | (d << 1) | u] & 0x0f);
        h[5] |= (uint8_t) ((((b & 0x1000) ? 1 : 0) | ((b & 0x2000) ? 2 : 0) | ((b & 0x4000) ? 4 : 0) | ((b & 0x8000) ? 8 : 0)) << 4);
        h[6] = ((b & 0x0100) ? 1 : 0) | ((b & 0x0200) ? 2 : 0) | ((dev->lt > 128) ? 4 : 0) | ((dev->rt > 128) ? 8 : 0) |
               ((b & 0x0020) ? 0x10 : 0) | ((b & 0x0010) ? 0x20 : 0) | ((b & 0x0040) ? 0x40 : 0) | ((b & 0x0080) ? 0x80 : 0);
        h[7] = 0;
        if (dev->have_sent && !memcmp(h, dev->sent, 8))
            return USB_DATA_NAK;
        memcpy(dev->sent, h, 8);
        dev->have_sent = 1;
        memcpy(buf, h, 8);
        return 8;
    }
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
xpad_data_out(usb_dev_t *usb, UNUSED(int ep), const uint8_t *buf, int len)
{
    xpad_t *dev = (xpad_t *) usb->priv;

    /* Xbox One: 05 20 xx 01 00 = power on (input starts); 360: LED / rumble commands */
    if ((dev->kind == 1) && (len >= 5) && (buf[0] == 0x05) && (buf[3] == 0x01))
        dev->started = 1;
    return 0;
}

static void
xpad_reset(usb_dev_t *usb)
{
    xpad_t *dev = (xpad_t *) usb->priv;

    dev->have_sent = 0;
    dev->started = 0;
    dev->guide_sent = 0;
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
    p          = getenv(dev->index ? "XPAD_KIND2" : "XPAD_KIND");
    dev->kind  = (p && !strcmp(p, "one")) ? 1 : (p && !strcmp(p, "hid")) ? 2 : 0;

    dev->usb.name      = dev->index ? "Xbox 360 Controller 2" : "Xbox 360 Controller";
    dev->usb.low_speed = 0;
    dev->usb.dev_desc  = xpad_dev_desc;
    dev->usb.cfg_desc  = xpad_cfg_desc;
    dev->usb.cfg_len   = XPAD_CFG_LEN;
    dev->usb.strings   = xpad_strings;
    dev->usb.n_strings = 3;
    if (dev->kind == 1) {
        dev->usb.dev_desc = one_dev_desc;
        dev->usb.cfg_desc = one_cfg_desc;
        dev->usb.cfg_len  = ONE_CFG_LEN;
    } else if (dev->kind == 2) {
        dev->usb.dev_desc  = hid_dev_desc;
        dev->usb.cfg_desc  = hid_cfg_desc;
        dev->usb.cfg_len   = HID_CFG_LEN;
        dev->usb.strings   = hid_strings;
        dev->usb.n_strings = 2;
    }
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
