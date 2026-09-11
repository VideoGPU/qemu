/*
 * MIPI CSI-2 TX AXI4-Lite IP attached to the NEORV32 external bus (XBUS)
 *
 * Copyright (c) 2026 Michael Levit
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_NEORV32_MIPI_TX_H
#define HW_NEORV32_MIPI_TX_H

#include "hw/sysbus.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_NEORV32_MIPI_TX "neorv32.mipi-tx"
OBJECT_DECLARE_SIMPLE_TYPE(Neorv32MipiTxState, NEORV32_MIPI_TX)

#define NEORV32_MIPI_TX_MMIO_SIZE 0x100

typedef struct Neorv32MipiTxState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    QEMUTimer frame_timer;

    char *dumpfile;
    FILE *dump;

    uint32_t control;
    uint32_t type_vc;
    uint16_t pixels_per_line;
    uint16_t n_lines;
    uint16_t frame_end_word;
    uint16_t tlp_sot_delay_clk;
    uint16_t tlpx_delay_clk;
    uint16_t tlp_sot_delay_data;
    uint16_t tlpx_delay_data;
    uint16_t tlp_sot_short;
    uint16_t ths_prepare;
    uint16_t ths_zero;
    uint16_t ths_exit;
    uint8_t n_mipi_lanes;

    bool hs_active;
    bool hs_data_valid;
    bool frame_done;
    bool irq_pending;

    uint16_t frame_number;
    uint32_t overlay_offset;
} Neorv32MipiTxState;

Neorv32MipiTxState *neorv32_mipi_tx_create(MemoryRegion *address_space,
                                           hwaddr base);

#endif /* HW_NEORV32_MIPI_TX_H */
