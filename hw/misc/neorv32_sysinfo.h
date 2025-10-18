#ifndef HW_NEORV32_SYSINFO_H
#define HW_NEORV32_SYSINFO_H

#include "system/memory.h"

/* Internal memory sizes */
#define SYSINFO_IMEM_SIZE 0x8000 /* 32K IMEM */
#define SYSINFO_DMEM_SIZE 0x8000 /* 32K DMEM */
#define SYSINFO_RVSG_SIZE 0x0    /* Not implemented*/

/* Define register values */
#define SYSINFO_CLK_HZ      (100000000U) /* 100 MHz */
#define SYSINFO_CACHE_VAL   (0U) /* No cache */

/**********************************************************************//**
 * @name Main Address Space Sections
 **************************************************************************/
/**@{*/
/** XIP-mapped memory base address */
#define NEORV32_XIP_MEM_BASE_ADDRESS    (0xE0000000U)
/** bootloader memory base address */
#define NEORV32_BOOTLOADER_BASE_ADDRESS (0xFFE00000U)
/** peripheral/IO devices memory base address */
#define NEORV32_IO_BASE_ADDRESS         (0XFFE00000U)
/**@}*/

#define NEORV32_IMEM_BASE 			 (0x00000000U)
#define NEORV32_DMEM_BASE 			 (0x80000000U)

/**********************************************************************//**
 * @name IO Address Space - Peripheral/IO Devices
 **************************************************************************/
//#define NEORV32_???_BASE   (0xFFE10000U) /**< reserved */
//#define NEORV32_???_BASE   (0xFFE20000U) /**< reserved */
//#define NEORV32_???_BASE   (0xFFE30000U) /**< reserved */
//#define NEORV32_???_BASE   (0xFFE40000U) /**< reserved */
//#define NEORV32_???_BASE   (0xFFE50000U) /**< reserved */
//#define NEORV32_???_BASE   (0xFFE60000U) /**< reserved */
//#define NEORV32_???_BASE   (0xFFE70000U) /**< reserved */
//#define NEORV32_???_BASE   (0xFFE80000U) /**< reserved */
//#define NEORV32_???_BASE   (0xFFE90000U) /**< reserved */
#define NEORV32_TWD_BASE     (0xFFEA0000U) /**< Two-Wire Device (TWD) */
#define NEORV32_CFS_BASE     (0xFFEB0000U) /**< Custom Functions Subsystem (CFS) */
#define NEORV32_SLINK_BASE   (0xFFEC0000U) /**< Stream Link Interface (SLINK) */
#define NEORV32_DMA_BASE     (0xFFED0000U) /**< Direct Memory Access Controller (DMA) */
#define NEORV32_CRC_BASE     (0xFFEE0000U) /**< Cyclic Redundancy Check Unit (DMA) */
#define NEORV32_XIP_BASE     (0xFFEF0000U) /**< Execute In Place Module (XIP) */
#define NEORV32_PWM_BASE     (0xFFF00000U) /**< Pulse Width Modulation Controller (PWM) */
#define NEORV32_GPTMR_BASE   (0xFFF10000U) /**< General Purpose Timer (GPTMR) */
#define NEORV32_ONEWIRE_BASE (0xFFF20000U) /**< 1-Wire Interface Controller (ONEWIRE) */
#define NEORV32_XIRQ_BASE    (0xFFF30000U) /**< External Interrupt Controller (XIRQ) */
#define NEORV32_MTIME_BASE   (0xFFF40000U) /**< Machine System Timer (MTIME) */
#define NEORV32_UART0_BASE   (0xFFF50000U) /**< Primary Universal Asynchronous Receiver and Transmitter (UART0) */
#define NEORV32_UART1_BASE   (0xFFF60000U) /**< Secondary Universal Asynchronous Receiver and Transmitter (UART1) */
#define NEORV32_SDI_BASE     (0xFFF70000U) /**< Serial Data Interface (SDI) */
#define NEORV32_SPI_BASE     (0xFFF80000U) /**< Serial Peripheral Interface Controller (SPI) */
#define NEORV32_TWI_BASE     (0xFFF90000U) /**< Two-Wire Interface Controller (TWI) */
#define NEORV32_TRNG_BASE    (0xFFFA0000U) /**< True Random Number Generator (TRNG) */
#define NEORV32_WDT_BASE     (0xFFFB0000U) /**< Watchdog Timer (WDT) */
#define NEORV32_GPIO_BASE    (0xFFFC0000U) /**< General Purpose Input/Output Port Controller (GPIO) */
#define NEORV32_NEOLED_BASE  (0xFFFD0000U) /**< Smart LED Hardware Interface (NEOLED) */
#define NEORV32_SYSINFO_BASE (0xFFFE0000U) /**< System Information Memory (SYSINFO) */
#define NEORV32_DM_BASE      (0xFFFF0000U) /**< On-Chip Debugger - Debug Module (OCD) */


void neorv32_sysinfo_create(MemoryRegion *address_space, hwaddr base);

#endif //HW_NEORV32_SYSINFO_H
