/* I2C master frontend over chardev/socket */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "chardev/char-fe.h"
#include "qemu/module.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "hw/i2c/i2c.h"

#define TYPE_I2C_MASTER_CHARDEV "i2c-master-chardev"
OBJECT_DECLARE_SIMPLE_TYPE(I2CMasterChardevState, I2C_MASTER_CHARDEV)

typedef struct I2CMasterChardevState {
    DeviceState parent_obj;

    CharFrontend chr;
    I2CBus *bus;

    char linebuf[1024];
    size_t line_len;
} I2CMasterChardevState;

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

static void i2c_master_chardev_do_write(I2CMasterChardevState *s,
                                        char **argv, int argc)
{
    uint8_t addr;
    size_t count, i;

    if (argc < 3 || !parse_u8(argv[1], &addr) || !parse_usize(argv[2], &count)) {
        i2c_master_chardev_reply(s, "ERR bad write syntax");
        return;
    }

    if ((size_t)(argc - 3) != count) {
        i2c_master_chardev_reply(s, "ERR byte count mismatch");
        return;
    }

    if (i2c_start_send(s->bus, addr)) {
        i2c_master_chardev_reply(s, "ERR address NACK");
        return;
    }

    for (i = 0; i < count; i++) {
        uint8_t data;

        if (!parse_u8(argv[i + 3], &data)) {
            i2c_end_transfer(s->bus);
            i2c_master_chardev_reply(s, "ERR invalid data byte");
            return;
        }

        if (i2c_send(s->bus, data)) {
            i2c_end_transfer(s->bus);
            i2c_master_chardev_reply(s, "ERR data NACK");
            return;
        }
    }

    i2c_end_transfer(s->bus);
    i2c_master_chardev_reply(s, "OK");
}

static void i2c_master_chardev_do_read(I2CMasterChardevState *s,
                                       char **argv, int argc)
{
    uint8_t addr;
    size_t count, i;
    GString *rsp;

    if (argc != 3 || !parse_u8(argv[1], &addr) || !parse_usize(argv[2], &count)) {
        i2c_master_chardev_reply(s, "ERR bad read syntax");
        return;
    }

    if (i2c_start_recv(s->bus, addr)) {
        i2c_master_chardev_reply(s, "ERR address NACK");
        return;
    }

    rsp = g_string_new("D");
    for (i = 0; i < count; i++) {
        uint8_t data = i2c_recv(s->bus);
        g_string_append_printf(rsp, " %02x", data);
    }

    i2c_nack(s->bus);
    i2c_end_transfer(s->bus);

    i2c_master_chardev_reply(s, rsp->str);
    g_string_free(rsp, true);
}

static void i2c_master_chardev_process_line(I2CMasterChardevState *s, char *line)
{
    char **raw;
    char *argv[300];
    int argc = 0;
    int i;

    raw = g_strsplit_set(line, " \t\r\n", -1);
    for (i = 0; raw[i]; i++) {
        if (raw[i][0] != '\0') {
            argv[argc++] = raw[i];
            if (argc >= G_N_ELEMENTS(argv)) {
                break;
            }
        }
    }

    if (argc == 0) {
        g_strfreev(raw);
        return;
    }

    if (!g_ascii_strcasecmp(argv[0], "W")) {
        i2c_master_chardev_do_write(s, argv, argc);
    } else if (!g_ascii_strcasecmp(argv[0], "R")) {
        i2c_master_chardev_do_read(s, argv, argc);
    } else {
        i2c_master_chardev_reply(s, "ERR unknown command");
    }

    g_strfreev(raw);
}

static int i2c_master_chardev_can_rx(void *opaque)
{
    return 4096;
}

static void i2c_master_chardev_rx(void *opaque, const uint8_t *buf, int size)
{
    I2CMasterChardevState *s = opaque;
    int i;

    for (i = 0; i < size; i++) {
        char ch = (char)buf[i];

        if (ch == '\n') {
            s->linebuf[s->line_len] = '\0';
            i2c_master_chardev_process_line(s, s->linebuf);
            s->line_len = 0;
            continue;
        }

        if (s->line_len + 1 >= sizeof(s->linebuf)) {
            s->line_len = 0;
            i2c_master_chardev_reply(s, "ERR line too long");
            continue;
        }

        s->linebuf[s->line_len++] = ch;
    }
}

static void i2c_master_chardev_event(void *opaque, QEMUChrEvent event)
{
}

static void i2c_master_chardev_realize(DeviceState *dev, Error **errp)
{
    I2CMasterChardevState *s = I2C_MASTER_CHARDEV(dev);

    s->bus = I2C_BUS(qdev_get_parent_bus(dev));
    qemu_chr_fe_set_handlers(&s->chr,
                             i2c_master_chardev_can_rx,
                             i2c_master_chardev_rx,
                             i2c_master_chardev_event,
                             NULL,
                             s,
                             NULL,
                             true);
}

static Property i2c_master_chardev_properties[] = {
    DEFINE_PROP_CHR("chardev", I2CMasterChardevState, chr),
};

static void i2c_master_chardev_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->bus_type = TYPE_I2C_BUS;
    dc->realize = i2c_master_chardev_realize;
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
