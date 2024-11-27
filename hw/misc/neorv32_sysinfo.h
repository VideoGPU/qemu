#ifndef HW_NEORV32_SYSINFO_H
#define HW_NEORV32_SYSINFO_H

#include "exec/memory.h"

/* Internal memory sizes */
#define SYSINFO_IMEM_SIZE 0x4000 /* 16K IMEM */
#define SYSINFO_DMEM_SIZE 0x4000 /* 16K DMEM */
#define SYSINFO_RVSG_SIZE 0x0    /* Not implemented*/

/* Define register values */
#define SYSINFO_CLK_HZ      (100000000U) // 100 MHz
#define SYSINFO_SOC_VAL     (1U << SYSINFO_SOC_IO_UART0) // Only UART enabled
#define SYSINFO_CACHE_VAL   (0U) // No cache

/**********************************************************************//**
 * @name Main Address Space Sections
 **************************************************************************/
/**@{*/
/** XIP-mapped memory base address */
#define NEORV32_XIP_MEM_BASE_ADDRESS    (0xE0000000U)
/** bootloader memory base address */
#define NEORV32_BOOTLOADER_BASE_ADDRESS (0xFFFFC000U)
/** peripheral/IO devices memory base address */
#define NEORV32_IO_BASE_ADDRESS         (0xFFFFE000U)
/**@}*/

#define NEORV32_IMEM_BASE 			 (0x00000000U)
#define NEORV32_DMEM_BASE 			 (0x80000000U)

/**********************************************************************//**
 * @name IO Address Space - Peripheral/IO Devices
 **************************************************************************/
/**@{*/
#define NEORV32_CFS_BASE     (0xFFFFEB00U) /**< Custom Functions Subsystem (CFS) */
#define NEORV32_SLINK_BASE   (0xFFFFEC00U) /**< Stream Link Interface (SLINK) */
#define NEORV32_DMA_BASE     (0xFFFFED00U) /**< Direct Memory Access Controller (DMA) */
#define NEORV32_CRC_BASE     (0xFFFFEE00U) /**< Cyclic Redundancy Check Unit (DMA) */
#define NEORV32_XIP_BASE     (0xFFFFEF00U) /**< Execute In Place Module (XIP) */
#define NEORV32_PWM_BASE     (0xFFFFF000U) /**< Pulse Width Modulation Controller (PWM) */
#define NEORV32_GPTMR_BASE   (0xFFFFF100U) /**< General Purpose Timer (GPTMR) */
#define NEORV32_ONEWIRE_BASE (0xFFFFF200U) /**< 1-Wire Interface Controller (ONEWIRE) */
#define NEORV32_XIRQ_BASE    (0xFFFFF300U) /**< External Interrupt Controller (XIRQ) */
#define NEORV32_MTIME_BASE   (0xFFFFF400U) /**< Machine System Timer (MTIME) */
#define NEORV32_UART0_BASE   (0xFFFFF500U) /**< Primary Universal Asynchronous Receiver and Transmitter (UART0) */
#define NEORV32_UART1_BASE   (0xFFFFF600U) /**< Secondary Universal Asynchronous Receiver and Transmitter (UART1) */
#define NEORV32_SDI_BASE     (0xFFFFF700U) /**< Serial Data Interface (SDI) */
#define NEORV32_SPI_BASE     (0xFFFFF800U) /**< Serial Peripheral Interface Controller (SPI) */
#define NEORV32_TWI_BASE     (0xFFFFF900U) /**< Two-Wire Interface Controller (TWI) */
#define NEORV32_TRNG_BASE    (0xFFFFFA00U) /**< True Random Number Generator (TRNG) */
#define NEORV32_WDT_BASE     (0xFFFFFB00U) /**< Watchdog Timer (WDT) */
#define NEORV32_GPIO_BASE    (0xFFFFFC00U) /**< General Purpose Input/Output Port Controller (GPIO) */
#define NEORV32_NEOLED_BASE  (0xFFFFFD00U) /**< Smart LED Hardware Interface (NEOLED) */
#define NEORV32_SYSINFO_BASE (0xFFFFFE00U) /**< System Information Memory (SYSINFO) */
#define NEORV32_DM_BASE      (0xFFFFFF00U) /**< On-Chip Debugger - Debug Module (OCD) */
#define NEORV32_SYSINFO_BASE (0xFFFFFE00U) /**< System Information Memory (SYSINFO) */


void neorv32_sysinfo_create(MemoryRegion *address_space, hwaddr base);

#endif //HW_NEORV32_SYSINFO_H
