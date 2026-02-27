/* Neorv32 Two-Wire Device (TWD) */

#ifndef HW_NEORV32_TWD_H
#define HW_NEORV32_TWD_H

#include "hw/sysbus.h"
#include "qemu/fifo8.h"
#include "qom/object.h"

#define TYPE_NEORV32_TWD "neorv32.twd"
OBJECT_DECLARE_SIMPLE_TYPE(Neorv32TWDState, NEORV32_TWD)

typedef struct Neorv32TWDState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    uint32_t ctrl;
    Fifo8 rx_fifo;
    Fifo8 tx_fifo;
} Neorv32TWDState;

Neorv32TWDState *neorv32_twd_create(MemoryRegion *address_space, hwaddr base);

#endif /* HW_NEORV32_TWD_H */
