
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

//TODO: do something with this relative path
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


static void neorv32_sysinfo_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    Neorv32SysInfoState *s = NEORV32_SYSINFO_QEMU(obj);

    memory_region_init_io(&s->mmio, OBJECT(s), &neorv32_sysinfo_ops, s,
                          TYPE_SIFIVE_UART, SIFIVE_UART_MAX);
    sysbus_init_mmio(sbd, &s->mmio);
}

static void neorv32_sysinfo_realize(DeviceState *dev, Error **errp)
{
    return;
}

static void neorv32_sysinfo_reset_enter(Object *obj, ResetType type)
{
    Neorv32SysInfoState *s = NEORV32_SYSINFO_QEMU(obj);

    s->clk_hz = SYSINFO_CLK_HZ;
    s->mem_val = SYSINFO_MEM_VAL;
    s->soc_features = SYSINFO_SOC_VAL;
    s->cache_features = SYSINFO_CACHE_VAL;
}

static void neorv32_sysinfo_reset_hold(Object *obj, ResetType type)
{
    return;
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
Neorv32SysInfoState *neorv32_sysinfo_create(MemoryRegion *address_space, hwaddr base)
{
    DeviceState *dev;
    SysBusDevice *s;

    dev = qdev_new(TYPE_NEORV32_SYSINFO_QEMU);
    s = SYS_BUS_DEVICE(dev);

    sysbus_realize_and_unref(s, &error_fatal);
    memory_region_add_subregion(address_space, base,
                                sysbus_mmio_get_region(s, 0));

    return NEORV32_SYSINFO_QEMU(dev);
}
