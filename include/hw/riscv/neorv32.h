/*
 * NEORV32 SOC presentation in QEMU
 *
 * Copyright (c) 2025 Michael Levit
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_NEORV32_H
#define HW_NEORV32_H

#include "hw/riscv/riscv_hart.h"
#include "hw/boards.h"

#if defined(TARGET_RISCV32)
#define NEORV32_CPU TYPE_RISCV_CPU_NEORV32
#endif

#define TYPE_RISCV_NEORV32_SOC "riscv.neorv32.soc"
#define RISCV_NEORV32_SOC(obj) \
    OBJECT_CHECK(Neorv32SoCState, (obj), TYPE_RISCV_NEORV32_SOC)

typedef struct Neorv32SoCState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    RISCVHartArrayState cpus;
    DeviceState *plic;
    MemoryRegion imem_region;
    MemoryRegion bootloader_rom;
    bool irq_connected_twd;
    bool irq_connected_uart0;
    bool irq_connected_spi0;
} Neorv32SoCState;

typedef struct Neorv32State {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/
    Neorv32SoCState soc;
} Neorv32State;

#define TYPE_NEORV32_MACHINE MACHINE_TYPE_NAME("neorv32")
#define NEORV32_MACHINE(obj) \
    OBJECT_CHECK(Neorv32State, (obj), TYPE_NEORV32_MACHINE)

enum {
    NEORV32_IMEM,
    NEORV32_BOOTLOADER_ROM,
    NEORV32_DMEM,
    NEORV32_SYSINFO,
    NEORV32_TWD_MMIO,
    NEORV32_UART0,
    NEORV32_SPI0,
};

/*
 * NEORV32 fast interrupt channels are wired to CPU local interrupt bits 16..31
 * (mie/mip CSR bits FIRQ0..FIRQ15).
 */
enum {
    NEORV32_FIRQ0  = 16,
    NEORV32_FIRQ1  = 17,
    NEORV32_FIRQ2  = 18,
    NEORV32_FIRQ3  = 19,
    NEORV32_FIRQ4  = 20,
    NEORV32_FIRQ5  = 21,
    NEORV32_FIRQ6  = 22,
    NEORV32_FIRQ7  = 23,
    NEORV32_FIRQ8  = 24,
    NEORV32_FIRQ9  = 25,
    NEORV32_FIRQ10 = 26,
    NEORV32_FIRQ11 = 27,
    NEORV32_FIRQ12 = 28,
    NEORV32_FIRQ13 = 29,
    NEORV32_FIRQ14 = 30,
    NEORV32_FIRQ15 = 31,
};

/* Named aliases (matching NEORV32 software API channel assignment). */
#define NEORV32_FIRQ_TWD     NEORV32_FIRQ0
#define NEORV32_FIRQ_CFS     NEORV32_FIRQ1
#define NEORV32_FIRQ_UART0   NEORV32_FIRQ2
#define NEORV32_FIRQ_UART1   NEORV32_FIRQ3
#define NEORV32_FIRQ_TRACER  NEORV32_FIRQ5
#define NEORV32_FIRQ_SPI     NEORV32_FIRQ6
#define NEORV32_FIRQ_TWI     NEORV32_FIRQ7
#define NEORV32_FIRQ_GPIO    NEORV32_FIRQ8
#define NEORV32_FIRQ_NEOLED  NEORV32_FIRQ9
#define NEORV32_FIRQ_DMA     NEORV32_FIRQ10
#define NEORV32_FIRQ_SDI     NEORV32_FIRQ11
#define NEORV32_FIRQ_GPTMR   NEORV32_FIRQ12
#define NEORV32_FIRQ_ONEWIRE NEORV32_FIRQ13
#define NEORV32_FIRQ_SLINK   NEORV32_FIRQ14
#define NEORV32_FIRQ_TRNG    NEORV32_FIRQ15

#endif /* HW_NEORV32_H */
