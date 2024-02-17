
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "migration/vmstate.h"
#include "chardev/char.h"
#include "chardev/char-fe.h"
#include "hw/irq.h"
#include "hw/char/sifive_uart.h"
#include "hw/qdev-properties-system.h"
#include "neorv32_sysinfo.h" //QEMU related

#include "/mnt/shonot/fpga_projects/neorv32/sw/lib/include/neorv32_sysinfo.h" //Neorv32 source tree

//Register addresses
enum {
	REG_SYSINFO_CLK        = 0,
	REG_SYSINFO_MEM        = 4,
	REG_SYSINFO_SOC        = 8,
	REG_SYSINFO_CACHE      = 12,
};

/* Registers values */
#define SYSINFO_CLK_HZ             (100000000) //100 MHz
#define SYSINFO_MEM_VAL            0x0F0F0F0F //Set all to 32KBytes 0xF = 15 = log2(32*1024)
#define SYSINFO_SOC_VAL            (1 << SYSINFO_SOC_IO_UART0) //Only UART enabled
#define SYSINFO_CACHE_VAL          (0) //No cache


/* Returns the state of the IP (interrupt pending) register */
//static uint64_t neorv32_sysinfo_ip(Neorv32SysInfoState *s)
//{
//    uint64_t ret = 0;
//
//    uint64_t txcnt = SIFIVE_UART_GET_TXCNT(s->txctrl);
//    uint64_t rxcnt = SIFIVE_UART_GET_RXCNT(s->rxctrl);
//
//    if (txcnt != 0) {
//        ret |= SIFIVE_UART_IP_TXWM;
//    }
//    if (s->rx_fifo_len > rxcnt) {
//        ret |= SIFIVE_UART_IP_RXWM;
//    }
//
//    return ret;
//}

//static void neorv32_sysinfo_update_irq(Neorv32SysInfoState *s)
//{
////    int cond = 0;
////    if ((s->ie & SIFIVE_UART_IE_TXWM) ||
////        ((s->ie & SIFIVE_UART_IE_RXWM) && s->rx_fifo_len)) {
////        cond = 1;
////    }
////    if (cond) {
////        qemu_irq_raise(s->irq);
////    } else {
////        qemu_irq_lower(s->irq);
////    }
//}


static uint64_t
neorv32_sysinfo_read(void *opaque, hwaddr addr, unsigned int size)
{
    Neorv32SysInfoState *s = opaque;
    switch (addr) {
    case REG_SYSINFO_CLK:
        return s->clk_hz;
    case REG_SYSINFO_MEM:
        return s->mem_val;
    case REG_SYSINFO_SOC:
        return s->soc_features;
    case REG_SYSINFO_CACHE:
        return s->cache_features;
    }

    qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read: addr=0x%x\n",
                  __func__, (int)addr);
    return 0;
}


static void
neorv32_sysinfo_write(void *opaque, hwaddr addr,
                  uint64_t val64, unsigned int size)
{
    Neorv32SysInfoState *s = opaque;
    uint32_t value = val64;
    switch (addr) {
    case REG_SYSINFO_CLK:
        s->clk_hz = val64;
        return;
    case REG_SYSINFO_MEM:
        s->mem_val = val64;
        return;
    case REG_SYSINFO_SOC:
        s->soc_features = val64;
        return;
    case REG_SYSINFO_CACHE:
        s->cache_features = val64;
        return;
    }
    qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write: addr=0x%x v=0x%x\n",
                  __func__, (int)addr, (int)value);
}

static const MemoryRegionOps neorv32_sysinfo_ops = {
    .read = neorv32_sysinfo_read,
    .write = neorv32_sysinfo_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

//static void neorv32_sysinfo_rx(void *opaque, const uint8_t *buf, int size)
//{
//    Neorv32SysInfoState *s = opaque;
////
////    /* Got a byte.  */
////    if (s->rx_fifo_len >= sizeof(s->rx_fifo)) {
////        printf("WARNING: UART dropped char.\n");
////        return;
////    }
////    s->rx_fifo[s->rx_fifo_len++] = *buf;
////
////    neorv32_sysinfo_update_irq(s);
//}


//static Property neorv32_sysinfo_properties[] = {
//    DEFINE_PROP_CHR("chardev", Neorv32SysInfoState, chr),
//    DEFINE_PROP_END_OF_LIST(),
//};

static void neorv32_sysinfo_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    Neorv32SysInfoState *s = NEORV32_SYSINFO_QEMU(obj);

    memory_region_init_io(&s->mmio, OBJECT(s), &neorv32_sysinfo_ops, s,
                          TYPE_SIFIVE_UART, SIFIVE_UART_MAX);
    sysbus_init_mmio(sbd, &s->mmio);
   // sysbus_init_irq(sbd, &s->irq);
}

static void neorv32_sysinfo_realize(DeviceState *dev, Error **errp)
{
 //   Neorv32SysInfoState *s = NEORV32_SYSINFO_QEMU(dev);

//    qemu_chr_fe_set_handlers(&s->chr, neorv32_sysinfo_can_rx, neorv32_sysinfo_rx,
//                             neorv32_sysinfo_event, neorv32_sysinfo_be_change, s,
//                             NULL, true);

}

static void neorv32_sysinfo_reset_enter(Object *obj, ResetType type)
{
    Neorv32SysInfoState *s = NEORV32_SYSINFO_QEMU(obj);

    s->clk_hz = SYSINFO_CLK_HZ;
    s->mem_val = SYSINFO_MEM_VAL;
    s->soc_features = SYSINFO_SOC_VAL;
    s->cache_features = SYSINFO_CACHE_VAL;
}

static void neorv32_sysinfo_reset_hold(Object *obj)
{
//    Neorv32SysInfoState *s = NEORV32_SYSINFO_QEMU(obj);
//    qemu_irq_lower(s->irq);
}

static const VMStateDescription vmstate_neorv32_sysinfo = {
    .name = TYPE_NEORV32_SYSINFO_QEMU,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(clk_hz, Neorv32SysInfoState),
        VMSTATE_UINT32(mem_val, Neorv32SysInfoState),
        VMSTATE_UINT32(soc_features, Neorv32SysInfoState),
        VMSTATE_UINT32(cache_features, Neorv32SysInfoState),
        VMSTATE_END_OF_LIST()
    },
};


static void neorv32_sysinfo_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    dc->realize = neorv32_sysinfo_realize;
    dc->vmsd = &vmstate_neorv32_sysinfo;
    rc->phases.enter = neorv32_sysinfo_reset_enter;
    rc->phases.hold  = neorv32_sysinfo_reset_hold;
    //device_class_set_props(dc, neorv32_sysinfo_properties);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo neorv32_sysinfo_info = {
    .name          = TYPE_NEORV32_SYSINFO_QEMU,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Neorv32SysInfoState),
    .instance_init = neorv32_sysinfo_init,
    .class_init    = neorv32_sysinfo_class_init,
};

static void neorv32_sysinfo_register_types(void)
{
    type_register_static(&neorv32_sysinfo_info);
}

type_init(neorv32_sysinfo_register_types)

/*
 * Create UART device.
 */
Neorv32SysInfoState *neorv32_sysinfo_create(MemoryRegion *address_space, hwaddr base,
    Chardev *chr, qemu_irq irq)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_new(TYPE_NEORV32_SYSINFO_QEMU);
    s = SYS_BUS_DEVICE(dev);
    //qdev_prop_set_chr(dev, "chardev", chr);
    sysbus_realize_and_unref(s, &error_fatal);
    memory_region_add_subregion(address_space, base,
                                sysbus_mmio_get_region(s, 0));
    //sysbus_connect_irq(s, 0, irq);

    return NEORV32_SYSINFO_QEMU(dev);
}
