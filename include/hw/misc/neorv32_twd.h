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
    qemu_irq sda_out;

    uint32_t ctrl;
    Fifo8 rx_fifo;
    Fifo8 tx_fifo;

    uint8_t scl_i;
    uint8_t sda_i;
    uint8_t sda_drv;
    uint8_t bitcnt;
    uint8_t shift;
    uint8_t bus_state;
    bool cmd_read;
    bool pending_addr_ack;
    bool pending_data_ack;
    bool state_busy;
} Neorv32TWDState;

Neorv32TWDState *neorv32_twd_create(MemoryRegion *address_space, hwaddr base);

#endif /* HW_NEORV32_TWD_H */
