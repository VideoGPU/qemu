
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

#define SIFIVE_UART_MAX  (32)

static Property neorv32_uart_properties[] = {
    DEFINE_PROP_CHR("chardev", Neorv32UARTState, chr),
    DEFINE_PROP_END_OF_LIST(),
};

static uint64_t
neorv32_uart_read(void *opaque, hwaddr addr, unsigned int size)
{

    return 17;
//	Neorv32UARTState *s = opaque;
//    unsigned char r;
//
//
//
//    qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read: addr=0x%x\n",
//                  __func__, (int)addr);
//    return 0;
}

static void
neorv32_uart_write(void *opaque, hwaddr addr,
                  uint64_t val64, unsigned int size)
{

    return;

//	Neorv32UARTState *s = opaque;
//    uint32_t value = val64;
//    unsigned char ch = value;
//
//    qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write: addr=0x%x v=0x%x\n",
//                  __func__, (int)addr, (int)value);
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
    		TYPE_NEORV32_UART, SIFIVE_UART_MAX);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
}

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
        VMSTATE_UINT8_ARRAY(rx_fifo, Neorv32UARTState,
        		NEORV32_UART_RX_FIFO_SIZE),
        VMSTATE_UINT8(rx_fifo_len, Neorv32UARTState),
        VMSTATE_UINT32(ie, Neorv32UARTState),
//        VMSTATE_UINT32(ip, SiFiveUARTState),
//        VMSTATE_UINT32(txctrl, SiFiveUARTState),
//        VMSTATE_UINT32(rxctrl, SiFiveUARTState),
//        VMSTATE_UINT32(div, SiFiveUARTState),
        VMSTATE_END_OF_LIST()
    },
};

static void neorv32_uart_reset_enter(Object *obj, ResetType type)
{
	Neorv32UARTState *s = NEORV32_UART(obj);
    s->rx_fifo_len = 0;
    s->ie = 0;
//    s->ip = 0;
//    s->txctrl = 0;
//    s->rxctrl = 0;
//    s->div = 0;
}

static void neorv32_uart_reset_hold(Object *obj)
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
Neorv32UARTState *neorv32_uart_create(MemoryRegion *address_space, hwaddr base,
    Chardev *chr, qemu_irq irq)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_new("riscv.neorv32.uart");
    s = SYS_BUS_DEVICE(dev);
    qdev_prop_set_chr(dev, "chardev", chr);
    sysbus_realize_and_unref(s, &error_fatal);
    memory_region_add_subregion(address_space, base,
                                sysbus_mmio_get_region(s, 0));
    sysbus_connect_irq(s, 0, irq);

    return NEORV32_UART(dev);
} //neorv32_uart_create
