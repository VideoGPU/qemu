
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "chardev/char.h"
#include "chardev/char-fe.h"
#include "hw/irq.h"
#include "hw/char/neorv32q_uart.h"
#include "hw/qdev-properties-system.h"

typedef volatile struct __attribute__((packed,aligned(4))) {
  uint32_t CTRL;  /**< offset 0: control register (#NEORV32_UART_CTRL_enum) */
  uint32_t DATA;  /**< offset 4: data register  (#NEORV32_UART_DATA_enum) */
} neorv32_uart_t;

#define NEORV32_UART_IO_REGION_SIZE  (32)

static Property neorv32_uart_properties[] = {
    DEFINE_PROP_CHR("chardev", Neorv32UARTState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

enum {
    NEORV32_UART_CTRL = 0,  /**< offset 0: control register */
    NEORV32_UART_DATA = 4  /**< offset 4: data register  */
};

/**  bits */
enum NEORV32_UART_CTRL_enum {
  UART_CTRL_EN            =  0, /**< (r/w): UART global enable */
  UART_CTRL_SIM_MODE      =  1, /**< rw Simulation output override enable */
  UART_CTRL_HWFC_EN       =  2, /**< rw Enable RTS/CTS hardware flow-control */
  UART_CTRL_PRSC0         =  3, /**< (r/w): clock prescaler select bit 0 */
  UART_CTRL_PRSC1         =  4, /**< (r/w): clock prescaler select bit 1 */
  UART_CTRL_PRSC2         =  5, /**< (r/w): clock prescaler select bit 2 */
  UART_CTRL_BAUD0         =  6, /**< (r/w): BAUD rate divisor, bit 0 */
  UART_CTRL_BAUD1         =  7, /**< (r/w): BAUD rate divisor, bit 1 */
  UART_CTRL_BAUD2         =  8, /**< (r/w): BAUD rate divisor, bit 2 */
  UART_CTRL_BAUD3         =  9, /**< (r/w): BAUD rate divisor, bit 3 */
  UART_CTRL_BAUD4         = 10, /**< (r/w): BAUD rate divisor, bit 4 */
  UART_CTRL_BAUD5         = 11, /**< (r/w): BAUD rate divisor, bit 5 */
  UART_CTRL_BAUD6         = 12, /**< (r/w): BAUD rate divisor, bit 6 */
  UART_CTRL_BAUD7         = 13, /**< (r/w): BAUD rate divisor, bit 7 */
  UART_CTRL_BAUD8         = 14, /**< (r/w): BAUD rate divisor, bit 8 */
  UART_CTRL_BAUD9         = 15, /**< (r/w): BAUD rate divisor, bit 9 */

  UART_CTRL_RX_NEMPTY     = 16, /**< (r/-): RX FIFO not empty */
  UART_CTRL_RX_HALF       = 17, /**< (r/-): RX FIFO at least half-full */
  UART_CTRL_RX_FULL       = 18, /**< (r/-): RX FIFO full */
  UART_CTRL_TX_EMPTY      = 19, /**< (r/-): TX FIFO empty */
  UART_CTRL_TX_NHALF      = 20, /**< (r/-): TX FIFO not at least half-full */
  UART_CTRL_TX_FULL       = 21, /**< (r/-): TX FIFO full */

  UART_CTRL_IRQ_RX_NEMPTY = 22, /* rw Fire IRQ if RX FIFO not empty */
  UART_CTRL_IRQ_RX_HALF   = 23, /* rw ... IRQ if RX FIFO at least half-full */
  UART_CTRL_IRQ_RX_FULL   = 24, /* rw ... IRQ if RX FIFO full */
  UART_CTRL_IRQ_TX_EMPTY  = 25, /* rw ... if TX FIFO empty */
  UART_CTRL_IRQ_TX_NHALF  = 26, /* rw ... if TX FIFO not at least half-full */

  UART_CTRL_RX_OVER       = 30, /**< (r/-): RX FIFO overflow */
  UART_CTRL_TX_BUSY       = 31  /**< (r/-): Tx busy or TX FIFO not empty */
};

/**  bits */
enum NEORV32_UART_DATA_enum {
  UART_DATA_RTX_LSB          =  0, /**< (r/w): UART rx/tx data, LSB */
  UART_DATA_RTX_MSB          =  7, /**< (r/w): UART rx/tx data, MSB */

  UART_DATA_RX_FIFO_SIZE_LSB =  8, /**< (r/-): log2(RX FIFO size), LSB */
  UART_DATA_RX_FIFO_SIZE_MSB = 11, /**< (r/-): log2(RX FIFO size), MSB */

  UART_DATA_TX_FIFO_SIZE_LSB = 12, /**< (r/-): log2(RX FIFO size), LSB */
  UART_DATA_TX_FIFO_SIZE_MSB = 15, /**< (r/-): log2(RX FIFO size), MSB */
};
/**@}*/

static void neorv32_uart_update_irq(Neorv32UARTState *s)
{
    int cond = 0;
    if ((s->ie & NEORV32_UART_IE_TXWM) ||
        ((s->ie & NEORV32_UART_IE_RXWM) && s->rx_fifo_len)) {
        cond = 1;
    }
    if (cond) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

static uint64_t
neorv32_uart_read(void *opaque, hwaddr addr, unsigned int size)
{
    Neorv32UARTState *s = opaque;
    unsigned char r;

    switch (addr) {
        case NEORV32_UART_CTRL:
        if (s->rx_fifo_len) {
            s->CTRL |= (1 << UART_CTRL_RX_NEMPTY); /* set data available */
        } else {
            s->CTRL &= ~(1 << UART_CTRL_RX_NEMPTY); /* clear data available */
        }
            return s->CTRL;
        case NEORV32_UART_DATA:
            if (s->rx_fifo_len) {
                r = s->rx_fifo[0];
                memmove(s->rx_fifo, s->rx_fifo + 1, s->rx_fifo_len - 1);
                s->rx_fifo_len--;
                qemu_chr_fe_accept_input(&s->chr);
                s->DATA = r; /* not sure need to store it */
                neorv32_uart_update_irq(s); /* TODO: check if need to call */
                return r;
            }
        }



    qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read: addr=0x%x\n",
                  __func__, (int)addr);
    return 0;
}



static void
neorv32_uart_write(void *opaque, hwaddr addr,
                  uint64_t val64, unsigned int size)
{

    Neorv32UARTState *s = opaque;
    uint32_t value = val64;
    unsigned char ch = value;

    /* TODO: check if need to update data and control bits */
    switch (addr) {
        case NEORV32_UART_CTRL:
            s->CTRL = value;
            /* TODO: check if need to call, depending on IRQ flags */
            /* neorv32_uart_update_irq(s); */
            return;
        case NEORV32_UART_DATA:
            s->DATA = value;
            qemu_chr_fe_write(&s->chr, &ch, 1);
            /* neorv32_uart_update_irq(s); TODO: check if need to call */
            return;
        }

    qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write: addr=0x%x v=0x%x\n",
                  __func__, (int)addr, (int)value);
}

static const MemoryRegionOps neorv32_uart_ops = {
    .read  = neorv32_uart_read,
    .write = neorv32_uart_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static void neorv32_uart_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    Neorv32UARTState *s = NEORV32_UART(obj);

    memory_region_init_io(&s->mmio, OBJECT(s), &neorv32_uart_ops, s,
                          TYPE_NEORV32_UART, NEORV32_UART_IO_REGION_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
}


static void neorv32_uart_rx(void *opaque, const uint8_t *buf, int size)
{
    Neorv32UARTState *s = opaque;

    /* Got a byte.  */
    if (s->rx_fifo_len >= sizeof(s->rx_fifo)) {
        printf("WARNING: UART dropped char.\n");
        return;
    }
    s->rx_fifo[s->rx_fifo_len++] = *buf;

    neorv32_uart_update_irq(s);
}

static int neorv32_uart_can_rx(void *opaque)
{
    Neorv32UARTState *s = opaque;

    return s->rx_fifo_len < sizeof(s->rx_fifo);
}

static void neorv32_uart_event(void *opaque, QEMUChrEvent event)
{
}

static int  neorv32_uart_be_change(void *opaque)
{
    Neorv32UARTState *s = opaque;

    qemu_chr_fe_set_handlers(&s->chr, neorv32_uart_can_rx, neorv32_uart_rx,
                             neorv32_uart_event, neorv32_uart_be_change, s,
                             NULL, true);

    return 0;
}

static void neorv32_uart_realize(DeviceState *dev, Error **errp)
{
    Neorv32UARTState *s = NEORV32_UART(dev);

    qemu_chr_fe_set_handlers(&s->chr, neorv32_uart_can_rx, neorv32_uart_rx,
                            neorv32_uart_event, neorv32_uart_be_change, s,
                             NULL, true);

}

static const VMStateDescription vmstate_neorv32_uart = {
    .name = TYPE_NEORV32_UART,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(rx_fifo,
                            Neorv32UARTState,
                            NEORV32_UART_RX_FIFO_SIZE),
        VMSTATE_UINT8(rx_fifo_len, Neorv32UARTState),
        VMSTATE_UINT32(ie, Neorv32UARTState),
        VMSTATE_END_OF_LIST()
    },
};

static void neorv32_uart_reset_enter(Object *obj, ResetType type)
{
    Neorv32UARTState *s = NEORV32_UART(obj);
    s->rx_fifo_len = 0;
    s->ie = 0;
}

static void neorv32_uart_reset_hold(Object *obj, ResetType type)
{
    Neorv32UARTState *s = NEORV32_UART(obj);
    qemu_irq_lower(s->irq);
}

static void neorv32_uart_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    dc->realize = neorv32_uart_realize;
    dc->vmsd = &vmstate_neorv32_uart;
    rc->phases.enter = neorv32_uart_reset_enter;
    rc->phases.hold  = neorv32_uart_reset_hold;
    device_class_set_props(dc, neorv32_uart_properties);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo neorv32_uart_info = {
    .name          = TYPE_NEORV32_UART,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Neorv32UARTState),
    .instance_init = neorv32_uart_init,
    .class_init    = neorv32_uart_class_init,
};

static void neorv32_uart_register_types(void)
{
    type_register_static(&neorv32_uart_info);
}

type_init(neorv32_uart_register_types)
/*
 * Create UART device.
 */
Neorv32UARTState *neorv32_uart_create(MemoryRegion *address_space,
                                      hwaddr base,
                                      Chardev *chr)
{
    DeviceState *dev;
    SysBusDevice *s;
    bool succed= false;

    dev = qdev_new("riscv.neorv32.uart");

    qdev_prop_set_chr(dev, "chardev", chr);
    s = SYS_BUS_DEVICE(dev);
    succed = sysbus_realize_and_unref(s, &error_fatal);

    if (succed) {
        memory_region_add_subregion(address_space, base,
                                    sysbus_mmio_get_region(s, 0));
        return NEORV32_UART(dev);
    } else {
        return NULL;
    }
} //neorv32_uart_create