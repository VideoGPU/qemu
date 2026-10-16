/*
 * NEORV32 XBUS multi-address I2C target with clock stretching
 * (i2c_target_ip: videogpu:user:i2c_target_axi_ip:1.0)
 *
 * Copyright (c) 2026 VideoGPU
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_NEORV32_I2C_TARGET_H
#define HW_NEORV32_I2C_TARGET_H

#include "hw/sysbus.h"
#include "hw/i2c/i2c.h"
#include "qemu/fifo8.h"
#include "qemu/fifo32.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_NEORV32_I2C_TARGET "neorv32.i2c-target"
OBJECT_DECLARE_SIMPLE_TYPE(Neorv32I2CTargetState, NEORV32_I2C_TARGET)

#define NEORV32_I2C_TARGET_MMIO_SIZE 0x1000
#define NEORV32_I2C_TARGET_SLOTS     8
#define NEORV32_I2C_TARGET_EVT_DEPTH 32
#define NEORV32_I2C_TARGET_TX_DEPTH  16

typedef struct Neorv32I2CTargetState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    I2CSlave *slave;
    QEMUTimer wdog_timer;

    /* registers */
    uint32_t ctrl;
    uint32_t irq_enable;
    uint32_t irq_sticky;        /* bits 1..3 of IRQ_STATUS */
    uint32_t filter_cycles;
    uint32_t sda_hold_cycles;
    uint32_t setup_cycles;
    uint32_t stretch_timeout;
    uint32_t stretch_max;
    uint32_t slot[NEORV32_I2C_TARGET_SLOTS];
    Fifo32 evt;
    Fifo8 tx;

    /* transfer state */
    bool in_transfer;
    bool rw;                    /* current transfer is a read */
    uint8_t cur_slot;
    uint8_t match_slot;
    bool txreq_sent;
    bool wd_tripped;
    bool rx_wait_ack;
    bool nack_given;
    bool ack_dec_valid;
    bool ack_dec_nack;
    int64_t stretch_start;      /* -1 when not stretching */
    int stretch_point;
} Neorv32I2CTargetState;

Neorv32I2CTargetState *neorv32_i2c_target_create(MemoryRegion *address_space,
                                                 hwaddr base, I2CBus *bus);

#endif
