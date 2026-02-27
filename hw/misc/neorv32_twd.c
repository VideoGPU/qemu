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

    /* Open-drain lines are high by default in this model. */
    ro |= (1U << TWD_CTRL_SENSE_SCL);
    ro |= (1U << TWD_CTRL_SENSE_SDA);

    /* No serial bus engine yet, so BUSY remains zero. */

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

    fifo8_create(&s->rx_fifo, NEORV32_TWD_RX_FIFO_CAPACITY);
    fifo8_create(&s->tx_fifo, NEORV32_TWD_TX_FIFO_CAPACITY);
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
