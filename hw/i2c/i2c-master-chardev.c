/*
 * I2C master frontend over chardev/socket
 *
 * One command per line; every command is one I2C transfer:
 *
 *   W <addr> <n> <b0> ... <bn-1>    write n bytes             -> OK
 *   R <addr> <n>                    read n bytes              -> D xx xx ...
 *   WR <addr> <nw> <b...> <nr>      write nw bytes, repeated
 *                                   START, read nr bytes      -> D xx ... (OK if nr = 0)
 *   errors: ERR address NACK | ERR data NACK | ERR timeout | ERR <syntax>
 *
 * The transfer runs byte by byte. Targets implementing TYPE_I2C_TARGET_STRETCH
 * (include/hw/i2c/i2c_stretch.h) may hold SCL low before a byte; the master
 * then waits on a virtual-clock timer, so the guest firmware runs, and gives
 * up after "stretch-timeout-ms". With "bus-hz" > 0 every byte additionally
 * takes 9 bit times of virtual time; the default 0 runs a transfer that no
 * target stretches in one go, as before. Input received while a transfer is
 * running is processed after it.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "chardev/char-fe.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/i2c/i2c.h"
#include "hw/i2c/i2c_stretch.h"

#define TYPE_I2C_MASTER_CHARDEV "i2c-master-chardev"
OBJECT_DECLARE_SIMPLE_TYPE(I2CMasterChardevState, I2C_MASTER_CHARDEV)

#define I2C_MASTER_MAX_SEGS   2
#define I2C_MASTER_MAX_BYTES  4096
#define I2C_MASTER_POLL_NS    1000      /* stretch poll interval */

typedef struct I2CMasterSeg {
    uint8_t addr;
    bool    read;
    size_t  n;
    uint8_t data[I2C_MASTER_MAX_BYTES];
} I2CMasterSeg;

typedef enum I2CMasterPhase {
    PH_IDLE, PH_START, PH_BYTE, PH_AFTER_SEND, PH_SEG_END, PH_DONE,
} I2CMasterPhase;

typedef struct I2CMasterChardevState {
    DeviceState parent_obj;

    CharFrontend chr;
    I2CBus *bus;
    uint32_t bus_hz;
    uint32_t stretch_timeout_ms;

    GByteArray *inbuf;
    QEMUTimer timer;

    /* transfer in progress */
    I2CMasterPhase phase;
    I2CMasterSeg seg[I2C_MASTER_MAX_SEGS];
    int nsegs;
    int cur;
    size_t idx;
    bool started;
    uint8_t started_addr;
    bool any_read;
    int64_t next_ns;            /* bus timing: earliest time for the next step */
    int64_t stretch_since;      /* -1: not waiting */
    GString *rsp;
} I2CMasterChardevState;

static void i2c_master_chardev_process_input(I2CMasterChardevState *s);

static bool parse_u8(const char *text, uint8_t *out)
{
    char *end = NULL;
    unsigned long val = strtoul(text, &end, 0);

    if (!text[0] || (end && *end) || val > 0xff) {
        return false;
    }

    *out = (uint8_t)val;
    return true;
}

static bool parse_usize(const char *text, size_t *out)
{
    char *end = NULL;
    unsigned long val = strtoul(text, &end, 0);

    if (!text[0] || (end && *end)) {
        return false;
    }

    *out = (size_t)val;
    return true;
}

static void i2c_master_chardev_reply(I2CMasterChardevState *s, const char *msg)
{
    qemu_chr_fe_printf(&s->chr, "%s\n", msg);
}

static int64_t byte_ns(I2CMasterChardevState *s)
{
    return s->bus_hz ? (int64_t)9 * NANOSECONDS_PER_SECOND / s->bus_hz : 0;
}

static void finish(I2CMasterChardevState *s, const char *err)
{
    if (s->started) {
        i2c_end_transfer(s->bus);
    }
    s->started = false;
    s->phase = PH_IDLE;
    timer_del(&s->timer);
    if (err) {
        i2c_master_chardev_reply(s, err);
    } else if (s->any_read) {
        i2c_master_chardev_reply(s, s->rsp->str);
    } else {
        i2c_master_chardev_reply(s, "OK");
    }
    g_string_truncate(s->rsp, 0);
    i2c_master_chardev_process_input(s);
    if (s->phase == PH_IDLE) {
        qemu_chr_fe_accept_input(&s->chr);
    }
}

/*
 * Wait while a target stretches at @point. Returns true if the caller must
 * return (timer armed or transfer aborted).
 */
static bool wait_stretch(I2CMasterChardevState *s, I2CStretchPoint point)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    if (!i2c_bus_stretched(s->bus, point)) {
        s->stretch_since = -1;
        return false;
    }
    if (s->stretch_since < 0) {
        s->stretch_since = now;
    } else if (now - s->stretch_since >
               (int64_t)s->stretch_timeout_ms * SCALE_MS) {
        s->stretch_since = -1;
        finish(s, "ERR timeout");
        return true;
    }
    timer_mod(&s->timer, now + I2C_MASTER_POLL_NS);
    return true;
}

static void run(I2CMasterChardevState *s)
{
    while (s->phase != PH_IDLE) {
        I2CMasterSeg *seg = &s->seg[s->cur];
        int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

        if (s->bus_hz && now < s->next_ns) {
            timer_mod(&s->timer, s->next_ns);
            return;
        }

        switch (s->phase) {
        case PH_START:
            if (s->started && seg->addr != s->started_addr) {
                /* QEMU keeps the matched targets on a repeated START */
                i2c_end_transfer(s->bus);
                s->started = false;
            }
            if (i2c_start_transfer(s->bus, seg->addr, seg->read)) {
                if (!s->started) {
                    s->phase = PH_IDLE;     /* the core ended it already */
                    i2c_master_chardev_reply(s, "ERR address NACK");
                    g_string_truncate(s->rsp, 0);
                    i2c_master_chardev_process_input(s);
                    if (s->phase == PH_IDLE) {
                        qemu_chr_fe_accept_input(&s->chr);
                    }
                } else {
                    finish(s, "ERR address NACK");
                }
                return;
            }
            s->started = true;
            s->started_addr = seg->addr;
            s->idx = 0;
            s->phase = PH_BYTE;
            s->next_ns = now + byte_ns(s);
            break;

        case PH_BYTE:
            if (s->idx >= seg->n) {
                s->phase = PH_SEG_END;
                break;
            }
            if (seg->read) {
                if (wait_stretch(s, I2C_STRETCH_BEFORE_RECV)) {
                    return;
                }
                g_string_append_printf(s->rsp, " %02x", i2c_recv(s->bus));
                s->idx++;
                s->next_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + byte_ns(s);
            } else {
                if (wait_stretch(s, I2C_STRETCH_BEFORE_SEND)) {
                    return;
                }
                if (i2c_send(s->bus, seg->data[s->idx])) {
                    finish(s, "ERR data NACK");
                    return;
                }
                s->phase = PH_AFTER_SEND;
            }
            break;

        case PH_AFTER_SEND:
            if (wait_stretch(s, I2C_STRETCH_AFTER_SEND)) {
                return;
            }
            if (i2c_bus_nacked(s->bus)) {
                finish(s, "ERR data NACK");
                return;
            }
            s->idx++;
            s->phase = PH_BYTE;
            s->next_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + byte_ns(s);
            break;

        case PH_SEG_END:
            if (seg->read && seg->n) {
                i2c_nack(s->bus);
            }
            s->cur++;
            s->phase = s->cur < s->nsegs ? PH_START : PH_DONE;
            break;

        case PH_DONE:
            finish(s, NULL);
            return;

        default:
            return;
        }
    }
}

static void timer_cb(void *opaque)
{
    run(opaque);
}

static bool parse_bytes(I2CMasterChardevState *s, I2CMasterSeg *seg,
                        char **argv, size_t n)
{
    size_t i;

    if (n > I2C_MASTER_MAX_BYTES) {
        i2c_master_chardev_reply(s, "ERR too many bytes");
        return false;
    }
    for (i = 0; i < n; i++) {
        if (!parse_u8(argv[i], &seg->data[i])) {
            i2c_master_chardev_reply(s, "ERR invalid data byte");
            return false;
        }
    }
    seg->n = n;
    return true;
}

static void begin(I2CMasterChardevState *s, int nsegs)
{
    s->nsegs = nsegs;
    s->cur = 0;
    s->started = false;
    s->stretch_since = -1;
    s->any_read = false;
    for (int i = 0; i < nsegs; i++) {
        s->any_read |= s->seg[i].read;
    }
    g_string_assign(s->rsp, "D");
    s->next_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    s->phase = PH_START;
    run(s);
}

static void i2c_master_chardev_process_line(I2CMasterChardevState *s, char *line)
{
    char **raw;
    char **argv;
    int argc = 0;
    int i;
    size_t n, nr;
    I2CMasterSeg *w = &s->seg[0], *r = &s->seg[1];

    raw = g_strsplit_set(line, " \t\r\n", -1);
    argv = g_new0(char *, g_strv_length(raw) + 1);
    for (i = 0; raw[i]; i++) {
        if (raw[i][0] != '\0') {
            argv[argc++] = raw[i];
        }
    }

    if (argc == 0) {
        goto out;
    }

    if (!g_ascii_strcasecmp(argv[0], "W")) {
        if (argc < 3 || !parse_u8(argv[1], &w->addr) || !parse_usize(argv[2], &n)) {
            i2c_master_chardev_reply(s, "ERR bad write syntax");
        } else if ((size_t)(argc - 3) != n) {
            i2c_master_chardev_reply(s, "ERR byte count mismatch");
        } else if (parse_bytes(s, w, &argv[3], n)) {
            w->read = false;
            begin(s, 1);
        }
    } else if (!g_ascii_strcasecmp(argv[0], "R")) {
        if (argc != 3 || !parse_u8(argv[1], &w->addr) || !parse_usize(argv[2], &n) ||
            n > I2C_MASTER_MAX_BYTES) {
            i2c_master_chardev_reply(s, "ERR bad read syntax");
        } else {
            w->read = true;
            w->n = n;
            begin(s, 1);
        }
    } else if (!g_ascii_strcasecmp(argv[0], "WR")) {
        if (argc < 4 || !parse_u8(argv[1], &w->addr) || !parse_usize(argv[2], &n) ||
            (size_t)argc != 4 + n || !parse_usize(argv[argc - 1], &nr) ||
            nr > I2C_MASTER_MAX_BYTES) {
            i2c_master_chardev_reply(s, "ERR bad write-read syntax");
        } else if (parse_bytes(s, w, &argv[3], n)) {
            w->read = false;
            r->addr = w->addr;
            r->read = true;
            r->n = nr;
            begin(s, nr ? 2 : 1);
        }
    } else {
        i2c_master_chardev_reply(s, "ERR unknown command");
    }

out:
    g_free(argv);
    g_strfreev(raw);
}

static void i2c_master_chardev_process_input(I2CMasterChardevState *s)
{
    while (s->phase == PH_IDLE) {
        guint8 *nl = memchr(s->inbuf->data, '\n', s->inbuf->len);
        size_t len;
        char *line;

        if (!nl) {
            if (s->inbuf->len >= 64 * 1024) {
                g_byte_array_set_size(s->inbuf, 0);
                i2c_master_chardev_reply(s, "ERR line too long");
            }
            return;
        }
        len = nl - s->inbuf->data;
        line = g_strndup((const char *)s->inbuf->data, len);
        g_byte_array_remove_range(s->inbuf, 0, len + 1);
        i2c_master_chardev_process_line(s, line);
        g_free(line);
    }
}

static int i2c_master_chardev_can_rx(void *opaque)
{
    I2CMasterChardevState *s = opaque;

    return s->phase == PH_IDLE ? 4096 : 0;
}

static void i2c_master_chardev_rx(void *opaque, const uint8_t *buf, int size)
{
    I2CMasterChardevState *s = opaque;

    g_byte_array_append(s->inbuf, buf, size);
    i2c_master_chardev_process_input(s);
}

static void i2c_master_chardev_event(void *opaque, QEMUChrEvent event)
{
}

static void i2c_master_chardev_realize(DeviceState *dev, Error **errp)
{
    I2CMasterChardevState *s = I2C_MASTER_CHARDEV(dev);

    s->bus = I2C_BUS(qdev_get_parent_bus(dev));
    s->inbuf = g_byte_array_new();
    s->rsp = g_string_new("");
    s->phase = PH_IDLE;
    timer_init_ns(&s->timer, QEMU_CLOCK_VIRTUAL, timer_cb, s);
    qemu_chr_fe_set_handlers(&s->chr,
                             i2c_master_chardev_can_rx,
                             i2c_master_chardev_rx,
                             i2c_master_chardev_event,
                             NULL,
                             s,
                             NULL,
                             true);
}

static void i2c_master_chardev_unrealize(DeviceState *dev)
{
    I2CMasterChardevState *s = I2C_MASTER_CHARDEV(dev);

    timer_del(&s->timer);
    g_byte_array_free(s->inbuf, true);
    g_string_free(s->rsp, true);
}

static const Property i2c_master_chardev_properties[] = {
    DEFINE_PROP_CHR("chardev", I2CMasterChardevState, chr),
    DEFINE_PROP_UINT32("bus-hz", I2CMasterChardevState, bus_hz, 0),
    DEFINE_PROP_UINT32("stretch-timeout-ms", I2CMasterChardevState,
                       stretch_timeout_ms, 1000),
};

static void i2c_master_chardev_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->bus_type = TYPE_I2C_BUS;
    dc->realize = i2c_master_chardev_realize;
    dc->unrealize = i2c_master_chardev_unrealize;
    device_class_set_props(dc, i2c_master_chardev_properties);
}

static const TypeInfo i2c_master_chardev_type_info = {
    .name = TYPE_I2C_MASTER_CHARDEV,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(I2CMasterChardevState),
    .class_init = i2c_master_chardev_class_init,
};

static void i2c_master_chardev_register_types(void)
{
    type_register_static(&i2c_master_chardev_type_info);
}

type_init(i2c_master_chardev_register_types)
