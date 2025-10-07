/*
 * QEMU implementation of the Neorv32 SPI block.
 *
 * Copyright (c) 2024 Michael Levit.
 *
 * Author:
 *   Michael Levit <michael@videogpu.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef NEORV32_SPI_H
#define NEORV32_SPI_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"

#define TYPE_NEORV32_SPI "neorv32.spi"
#define NEORV32_SPI(obj) OBJECT_CHECK(NEORV32SPIState, (obj), TYPE_NEORV32_SPI)

typedef struct NEORV32SPIState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    qemu_irq irq;
    SSIBus *bus;

    qemu_irq *cs_lines;
    uint32_t num_cs;

    uint32_t ctrl;
    uint32_t data;

    Fifo8 tx_fifo;
    Fifo8 rx_fifo;
    int fifo_capacity;

    /* Command-mode CS tracking */
    uint8_t active_cs;   /* 0..7 selected via DATA[2:0] */
    bool    cs_asserted; /* true if command-mode asserted CS */
} NEORV32SPIState;




NEORV32SPIState *neorv32_spi_create(MemoryRegion *sys_mem, hwaddr base_addr);

#endif /* NEORV32_SPI_H */
