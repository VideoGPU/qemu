/* Neorv32 Two-Wire Device (TWD) */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "hw/misc/neorv32_twd.h"

#define NEORV32_TWD_MMIO_SIZE 0x8

#define NEORV32_TWD_RX_FIFO_CAPACITY 16
#define NEORV32_TWD_TX_FIFO_CAPACITY 16

enum {
    NEORV32_TWD_CTRL = 0x0,
    NEORV32_TWD_DATA = 0x4,
};

enum {
    TWD_LINE_SCL = 0,
    TWD_LINE_SDA = 1,
};

typedef enum Neorv32TWDBusState {
    TWD_BUS_IDLE = 0,
    TWD_BUS_ADDR,
    TWD_BUS_ADDR_ACK,
    TWD_BUS_RX,
    TWD_BUS_RX_ACK,
    TWD_BUS_TX,
    TWD_BUS_TX_ACK,
} Neorv32TWDBusState;

static void neorv32_twd_update_irq(Neorv32TWDState *s);
static void neorv32_twd_update_ctrl_status(Neorv32TWDState *s);

enum NEORV32_TWD_CTRL_BITS {
    TWD_CTRL_EN           = 0,
    TWD_CTRL_CLR_RX       = 1,
    TWD_CTRL_CLR_TX       = 2,
    TWD_CTRL_FSEL         = 3,
    TWD_CTRL_DEV_ADDR0    = 4,
    TWD_CTRL_DEV_ADDR6    = 10,
    TWD_CTRL_IRQ_RX_AVAIL = 11,
    TWD_CTRL_IRQ_RX_FULL  = 12,
    TWD_CTRL_IRQ_TX_EMPTY = 13,

    TWD_CTRL_RX_FIFO_LSB  = 16,
    TWD_CTRL_RX_FIFO_MSB  = 19,
    TWD_CTRL_TX_FIFO_LSB  = 20,
    TWD_CTRL_TX_FIFO_MSB  = 23,

    TWD_CTRL_RX_AVAIL     = 25,
    TWD_CTRL_RX_FULL      = 26,
    TWD_CTRL_TX_EMPTY     = 27,
    TWD_CTRL_TX_FULL      = 28,
    TWD_CTRL_SENSE_SCL    = 29,
    TWD_CTRL_SENSE_SDA    = 30,
    TWD_CTRL_BUSY         = 31,
};

static inline bool twd_get_bit(uint32_t v, unsigned int bit)
{
    return (v >> bit) & 0x1U;
}

static inline unsigned int twd_log2_u32(uint32_t x)
{
    unsigned int v = 0;

    while (x > 1) {
        x >>= 1;
        v++;
    }

    return v;
}

static inline uint8_t twd_device_addr(Neorv32TWDState *s)
{
    return (s->ctrl >> TWD_CTRL_DEV_ADDR0) & 0x7FU;
}

static inline bool twd_enabled(Neorv32TWDState *s)
{
    return twd_get_bit(s->ctrl, TWD_CTRL_EN);
}

static inline Neorv32TWDBusState twd_bus_state(Neorv32TWDState *s)
{
    return (Neorv32TWDBusState)s->bus_state;
}

static inline void twd_set_bus_state(Neorv32TWDState *s, Neorv32TWDBusState st)
{
    s->bus_state = (uint8_t)st;
}

static void neorv32_twd_drive_sda(Neorv32TWDState *s, uint8_t level)
{
    s->sda_drv = level ? 1 : 0;
    qemu_set_irq(s->sda_out, s->sda_drv);
}

static void neorv32_twd_bus_reset(Neorv32TWDState *s)
{
    s->bitcnt = 0;
    s->shift = 0;
    s->cmd_read = false;
    s->pending_addr_ack = false;
    s->pending_data_ack = false;
    s->state_busy = false;
    twd_set_bus_state(s, TWD_BUS_IDLE);
    neorv32_twd_drive_sda(s, 1);
}

static void neorv32_twd_push_rx(Neorv32TWDState *s, uint8_t value)
{
    if (!fifo8_is_full(&s->rx_fifo)) {
        fifo8_push(&s->rx_fifo, value);
    }
}

static uint8_t neorv32_twd_peek_or_dummy_tx(Neorv32TWDState *s)
{
    uint8_t value;

    if (fifo8_is_empty(&s->tx_fifo)) {
        return 0xFF;
    }

    value = fifo8_pop(&s->tx_fifo);
    return value;
}

static void neorv32_twd_handle_start(Neorv32TWDState *s)
{
    if (!twd_enabled(s)) {
        return;
    }

    s->bitcnt = 0;
    s->shift = 0;
    s->cmd_read = false;
    s->pending_addr_ack = false;
    s->pending_data_ack = false;
    s->state_busy = true;
    twd_set_bus_state(s, TWD_BUS_ADDR);
    neorv32_twd_drive_sda(s, 1);
}

static void neorv32_twd_handle_stop(Neorv32TWDState *s)
{
    neorv32_twd_bus_reset(s);
}

static void neorv32_twd_line_step(Neorv32TWDState *s, bool scl_rise, bool scl_fall)
{
    Neorv32TWDBusState state;

    if (!twd_enabled(s)) {
        neorv32_twd_bus_reset(s);
        return;
    }

    state = twd_bus_state(s);

    switch (state) {
    case TWD_BUS_IDLE:
        break;

    case TWD_BUS_ADDR:
        if (scl_rise) {
            s->shift = (s->shift << 1) | (s->sda_i & 0x1U);
            s->bitcnt++;
            if (s->bitcnt == 8) {
                uint8_t addr = s->shift >> 1;

                s->cmd_read = s->shift & 0x1U;
                if (addr == twd_device_addr(s)) {
                    s->pending_addr_ack = true;
                    twd_set_bus_state(s, TWD_BUS_ADDR_ACK);
                } else {
                    neorv32_twd_bus_reset(s);
                }
                s->bitcnt = 0;
                s->shift = 0;
            }
        }
        break;

    case TWD_BUS_ADDR_ACK:
        if (scl_fall && s->pending_addr_ack && (s->bitcnt == 0)) {
            neorv32_twd_drive_sda(s, 0);
            s->bitcnt = 1;
        } else if (scl_fall && s->pending_addr_ack && (s->bitcnt == 1)) {
            neorv32_twd_drive_sda(s, 1);
            s->pending_addr_ack = false;
            s->bitcnt = 0;
            if (s->cmd_read) {
                s->shift = neorv32_twd_peek_or_dummy_tx(s);
                twd_set_bus_state(s, TWD_BUS_TX);
                neorv32_twd_drive_sda(s, (s->shift & 0x80U) ? 1 : 0);
            } else {
                s->shift = 0;
                twd_set_bus_state(s, TWD_BUS_RX);
            }
        }
        break;

    case TWD_BUS_RX:
        if (scl_rise) {
            s->shift = (s->shift << 1) | (s->sda_i & 0x1U);
            s->bitcnt++;
            if (s->bitcnt == 8) {
                s->pending_data_ack = !fifo8_is_full(&s->rx_fifo);
                twd_set_bus_state(s, TWD_BUS_RX_ACK);
            }
        }
        break;

    case TWD_BUS_RX_ACK:
        if (scl_fall && (s->bitcnt == 8)) {
            neorv32_twd_drive_sda(s, s->pending_data_ack ? 0 : 1);
            s->bitcnt = 9;
        } else if (scl_fall && (s->bitcnt == 9)) {
            if (s->pending_data_ack) {
                neorv32_twd_push_rx(s, s->shift);
            }
            neorv32_twd_drive_sda(s, 1);
            s->pending_data_ack = false;
            s->bitcnt = 0;
            s->shift = 0;
            twd_set_bus_state(s, TWD_BUS_RX);
        }
        break;

    case TWD_BUS_TX:
        if (scl_fall) {
            neorv32_twd_drive_sda(s, (s->shift & 0x80U) ? 1 : 0);
        }
        if (scl_rise) {
            s->shift <<= 1;
            s->bitcnt++;
            if (s->bitcnt == 8) {
                twd_set_bus_state(s, TWD_BUS_TX_ACK);
                neorv32_twd_drive_sda(s, 1);
                s->bitcnt = 0;
            }
        }
        break;

    case TWD_BUS_TX_ACK:
        if (scl_rise) {
            if (s->sda_i == 0) {
                s->shift = neorv32_twd_peek_or_dummy_tx(s);
                twd_set_bus_state(s, TWD_BUS_TX);
                neorv32_twd_drive_sda(s, (s->shift & 0x80U) ? 1 : 0);
            } else {
                neorv32_twd_bus_reset(s);
            }
        }
        break;

    default:
        neorv32_twd_bus_reset(s);
        break;
    }
}

static void neorv32_twd_line_in(void *opaque, int n, int level)
{
    Neorv32TWDState *s = opaque;
    uint8_t old_scl = s->scl_i;
    uint8_t old_sda = s->sda_i;
    bool scl_rise, scl_fall, start_cond, stop_cond;

    if (n == TWD_LINE_SCL) {
        s->scl_i = level ? 1 : 0;
    } else if (n == TWD_LINE_SDA) {
        s->sda_i = level ? 1 : 0;
    } else {
        return;
    }

    scl_rise = (!old_scl && s->scl_i);
    scl_fall = (old_scl && !s->scl_i);
    start_cond = old_sda && !s->sda_i && s->scl_i;
    stop_cond = !old_sda && s->sda_i && s->scl_i;

    if (start_cond) {
        neorv32_twd_handle_start(s);
    } else if (stop_cond) {
        neorv32_twd_handle_stop(s);
    }

    neorv32_twd_line_step(s, scl_rise, scl_fall);
    neorv32_twd_update_ctrl_status(s);
    neorv32_twd_update_irq(s);
}

static void neorv32_twd_update_irq(Neorv32TWDState *s)
{
    bool enabled = twd_get_bit(s->ctrl, TWD_CTRL_EN);
    bool rx_avail = !fifo8_is_empty(&s->rx_fifo);
    bool rx_full = fifo8_is_full(&s->rx_fifo);
    bool tx_empty = fifo8_is_empty(&s->tx_fifo);

    bool level = enabled && (
        (twd_get_bit(s->ctrl, TWD_CTRL_IRQ_RX_AVAIL) && rx_avail) ||
        (twd_get_bit(s->ctrl, TWD_CTRL_IRQ_RX_FULL) && rx_full) ||
        (twd_get_bit(s->ctrl, TWD_CTRL_IRQ_TX_EMPTY) && tx_empty)
    );

    qemu_set_irq(s->irq, level ? 1 : 0);
}

static void neorv32_twd_update_ctrl_status(Neorv32TWDState *s)
{
    uint32_t ro = 0;
    bool rx_avail = !fifo8_is_empty(&s->rx_fifo);
    bool rx_full = fifo8_is_full(&s->rx_fifo);
    bool tx_empty = fifo8_is_empty(&s->tx_fifo);
    bool tx_full = fifo8_is_full(&s->tx_fifo);
    uint32_t rx_fifo_log2 = twd_log2_u32(NEORV32_TWD_RX_FIFO_CAPACITY) & 0xFU;
    uint32_t tx_fifo_log2 = twd_log2_u32(NEORV32_TWD_TX_FIFO_CAPACITY) & 0xFU;

    ro |= (rx_fifo_log2 << TWD_CTRL_RX_FIFO_LSB);
    ro |= (tx_fifo_log2 << TWD_CTRL_TX_FIFO_LSB);

    if (rx_avail) {
        ro |= (1U << TWD_CTRL_RX_AVAIL);
    }
    if (rx_full) {
        ro |= (1U << TWD_CTRL_RX_FULL);
    }
    if (tx_empty) {
        ro |= (1U << TWD_CTRL_TX_EMPTY);
    }
    if (tx_full) {
        ro |= (1U << TWD_CTRL_TX_FULL);
    }

    if (s->scl_i) {
        ro |= (1U << TWD_CTRL_SENSE_SCL);
    }
    if (s->sda_i) {
        ro |= (1U << TWD_CTRL_SENSE_SDA);
    }
    if (s->state_busy) {
        ro |= (1U << TWD_CTRL_BUSY);
    }

    s->ctrl &= 0x00003ff9U;
    s->ctrl |= ro;
}

static uint64_t neorv32_twd_read(void *opaque, hwaddr addr, unsigned int size)
{
    Neorv32TWDState *s = opaque;
    uint32_t val = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid read size %u at 0x%" HWADDR_PRIx "\n",
                      __func__, size, addr);
        return 0;
    }

    switch (addr) {
    case NEORV32_TWD_CTRL:
        neorv32_twd_update_ctrl_status(s);
        val = s->ctrl;
        break;
    case NEORV32_TWD_DATA:
        if (!fifo8_is_empty(&s->rx_fifo)) {
            val = fifo8_pop(&s->rx_fifo);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad read at 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }

    neorv32_twd_update_ctrl_status(s);
    neorv32_twd_update_irq(s);

    return val;
}

static void neorv32_twd_write(void *opaque, hwaddr addr,
                              uint64_t val64, unsigned int size)
{
    Neorv32TWDState *s = opaque;
    uint32_t val = (uint32_t)val64;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid write size %u at 0x%" HWADDR_PRIx "\n",
                      __func__, size, addr);
        return;
    }

    switch (addr) {
    case NEORV32_TWD_CTRL: {
        uint32_t rw_mask =
            (1U << TWD_CTRL_EN) |
            (1U << TWD_CTRL_FSEL) |
            (((1U << (TWD_CTRL_DEV_ADDR6 - TWD_CTRL_DEV_ADDR0 + 1)) - 1)
             << TWD_CTRL_DEV_ADDR0) |
            (1U << TWD_CTRL_IRQ_RX_AVAIL) |
            (1U << TWD_CTRL_IRQ_RX_FULL) |
            (1U << TWD_CTRL_IRQ_TX_EMPTY);

        bool clr_rx = twd_get_bit(val, TWD_CTRL_CLR_RX);
        bool clr_tx = twd_get_bit(val, TWD_CTRL_CLR_TX);

        s->ctrl = (s->ctrl & ~rw_mask) | (val & rw_mask);

        if (!twd_get_bit(s->ctrl, TWD_CTRL_EN) || clr_rx) {
            fifo8_reset(&s->rx_fifo);
        }

        if (!twd_get_bit(s->ctrl, TWD_CTRL_EN) || clr_tx) {
            fifo8_reset(&s->tx_fifo);
        }

        if (!twd_get_bit(s->ctrl, TWD_CTRL_EN)) {
            neorv32_twd_bus_reset(s);
        }

        break;
    }
    case NEORV32_TWD_DATA:
        if (twd_get_bit(s->ctrl, TWD_CTRL_EN) && !fifo8_is_full(&s->tx_fifo)) {
            fifo8_push(&s->tx_fifo, (uint8_t)val);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad write at 0x%" HWADDR_PRIx " value=0x%08x\n",
                      __func__, addr, val);
        break;
    }

    neorv32_twd_update_ctrl_status(s);
    neorv32_twd_update_irq(s);
}

static const MemoryRegionOps neorv32_twd_ops = {
    .read = neorv32_twd_read,
    .write = neorv32_twd_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void neorv32_twd_reset(DeviceState *dev)
{
    Neorv32TWDState *s = NEORV32_TWD(dev);

    s->ctrl = 0;
    fifo8_reset(&s->rx_fifo);
    fifo8_reset(&s->tx_fifo);
    s->scl_i = 1;
    s->sda_i = 1;
    neorv32_twd_bus_reset(s);

    neorv32_twd_update_ctrl_status(s);
    neorv32_twd_update_irq(s);
}

static void neorv32_twd_init(Object *obj)
{
    Neorv32TWDState *s = NEORV32_TWD(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, OBJECT(s), &neorv32_twd_ops, s,
                          TYPE_NEORV32_TWD, NEORV32_TWD_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
    qdev_init_gpio_in_named(DEVICE(obj), neorv32_twd_line_in, "line", 2);
    qdev_init_gpio_out_named(DEVICE(obj), &s->sda_out, "sda-out", 1);

    fifo8_create(&s->rx_fifo, NEORV32_TWD_RX_FIFO_CAPACITY);
    fifo8_create(&s->tx_fifo, NEORV32_TWD_TX_FIFO_CAPACITY);

    s->scl_i = 1;
    s->sda_i = 1;
    neorv32_twd_bus_reset(s);
}

static void neorv32_twd_finalize(Object *obj)
{
    Neorv32TWDState *s = NEORV32_TWD(obj);

    fifo8_destroy(&s->rx_fifo);
    fifo8_destroy(&s->tx_fifo);
}

static void neorv32_twd_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, neorv32_twd_reset);
}

static const TypeInfo neorv32_twd_type_info = {
    .name = TYPE_NEORV32_TWD,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Neorv32TWDState),
    .instance_init = neorv32_twd_init,
    .instance_finalize = neorv32_twd_finalize,
    .class_init = neorv32_twd_class_init,
};

static void neorv32_twd_register_types(void)
{
    type_register_static(&neorv32_twd_type_info);
}

type_init(neorv32_twd_register_types)

Neorv32TWDState *neorv32_twd_create(MemoryRegion *address_space, hwaddr base)
{
    DeviceState *dev;
    SysBusDevice *sbd;
    bool success;

    dev = qdev_new(TYPE_NEORV32_TWD);
    sbd = SYS_BUS_DEVICE(dev);
    success = sysbus_realize_and_unref(sbd, &error_fatal);

    if (!success) {
        return NULL;
    }

    memory_region_add_subregion(address_space, base, sysbus_mmio_get_region(sbd, 0));
    return NEORV32_TWD(dev);
}
