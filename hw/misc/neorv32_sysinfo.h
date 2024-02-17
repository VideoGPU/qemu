#ifndef HW_NEORV32_SYSINFO_H
#define HW_NEORV32_SYSINFO_H

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



#define TYPE_NEORV32_SYSINFO_QEMU "riscv.neorv32.sysinfo"
OBJECT_DECLARE_SIMPLE_TYPE(Neorv32SysInfoState, NEORV32_SYSINFO_QEMU)


struct Neorv32SysInfoState{
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    uint32_t clk_hz;
    uint32_t mem_val;
    uint32_t soc_features;
    uint32_t cache_features;
};

Neorv32SysInfoState *neorv32_sysinfo_create(MemoryRegion *address_space, hwaddr base,
    Chardev *chr, qemu_irq irq);


#endif //HW_NEORV32_SYSINFO_H
