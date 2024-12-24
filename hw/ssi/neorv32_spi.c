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


/*
 * QEMU model of a NEORV32 SPI Controller
 *
 * This example is inspired by the SiFive SPI controller implementation shown
 * previously and adapted to the NEORV32 SPI register interface and semantics.
 *
 * IMPORTANT:
 * This code is an illustrative example. Adjust register addresses, IRQ logic,
 * FIFO sizes, and chip select configurations according to actual NEORV32 SPI
 * specifications. The following is based on the given register bits and a
 * presumed memory map. Check the official NEORV32 documentation for the
 * correct register definitions, addressing scheme, and functionality.
 *
 * The code simulates:
 *  - A single SPI control register (CTRL) and a data register (DATA).
 *  - TX and RX FIFOs for SPI transfers.
 *  - Basic SPI master logic (no advanced timing or prescaler logic shown).
 *  - Chip select lines and interrupts based on FIFO status.
 *
 * This code will:
 *   - Create a QEMU device "neorv32-spi"
 *   - Map it to a 0x1000 address space region
 *   - Provide a simple SPI master interface using QEMU’s ssi bus
 *   - Allow reading/writing CTRL and DATA registers
 *   - Simulate FIFO behavior and trigger IRQ lines
 */

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qemu/fifo8.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace/trace-root.h"
#include "qapi/error.h"
#include "hw/irq.h"
#include "hw/ssi/neorv32_spi.h"



/* SPI control register bits */
enum NEORV32_SPI_CTRL_enum {
  SPI_CTRL_EN           =  0, /**< enable SPI unit */
  SPI_CTRL_CPHA         =  1, /**< clock phase */
  SPI_CTRL_CPOL         =  2, /**< clock polarity */
  SPI_CTRL_CS_SEL0      =  3, /**< CS bit 0 */
  SPI_CTRL_CS_SEL1      =  4, /**< CS bit 1 */
  SPI_CTRL_CS_SEL2      =  5, /**< CS bit 2 */
  SPI_CTRL_CS_EN        =  6, /**< CS enable (active low if set) */
  SPI_CTRL_PRSC0        =  7, /**< prescaler bits... */
  SPI_CTRL_PRSC1        =  8,
  SPI_CTRL_PRSC2        =  9,
  SPI_CTRL_CDIV0        = 10,
  SPI_CTRL_CDIV1        = 11,
  SPI_CTRL_CDIV2        = 12,
  SPI_CTRL_CDIV3        = 13,

  SPI_CTRL_RX_AVAIL     = 16, /**< RX FIFO data available (read-only status) */
  SPI_CTRL_TX_EMPTY     = 17, /**< TX FIFO empty (read-only status) */
  SPI_CTRL_TX_NHALF     = 18, /**< TX FIFO not at least half full */
  SPI_CTRL_TX_FULL      = 19, /**< TX FIFO full (read-only status) */

  SPI_CTRL_IRQ_RX_AVAIL = 20, /**< IRQ if RX data available */
  SPI_CTRL_IRQ_TX_EMPTY = 21, /**< IRQ if TX empty */
  SPI_CTRL_IRQ_TX_HALF  = 22, /**< IRQ if TX < half full */

  SPI_CTRL_FIFO_LSB     = 23, /**< log2(FIFO size) lsb */
  SPI_CTRL_FIFO_MSB     = 26, /**< log2(FIFO size) msb */

  SPI_CTRL_BUSY         = 31  /**< SPI busy flag (read-only status) */
};

/* Register offsets */
#define NEORV32_SPI_CTRL  0x00
#define NEORV32_SPI_DATA  0x04
#define NEORV32_SPI_MMIO_SIZE   0x8  // ctrl + data (8 bytes total)

/* Utility functions to get/set bits in ctrl register */
static inline bool get_ctrl_bit(NEORV32SPIState *s, int bit)
{
    return (s->ctrl & (1 << bit)) != 0;
}

static inline void set_ctrl_bit(NEORV32SPIState *s, int bit, bool val)
{
    if (val) {
        s->ctrl |= (1 << bit);
    } else {
        s->ctrl &= ~(1 << bit);
    }
}

/* Update read-only status bits in CTRL register */
static void neorv32_spi_update_status(NEORV32SPIState *s)
{
    /* RX_AVAIL: set if RX FIFO not empty */
    set_ctrl_bit(s, SPI_CTRL_RX_AVAIL, !fifo8_is_empty(&s->rx_fifo));

    /* TX_EMPTY: set if TX FIFO empty */
    set_ctrl_bit(s, SPI_CTRL_TX_EMPTY, fifo8_is_empty(&s->tx_fifo));

    /* TX_FULL: set if TX FIFO full */
    set_ctrl_bit(s, SPI_CTRL_TX_FULL, fifo8_is_full(&s->tx_fifo));

    /* TX_NHALF: set if TX FIFO not at least half full */
    /* Half full means: #used >= capacity/2. So not half full = #used < capacity/2 */
    int used = fifo8_num_used(&s->tx_fifo);
    bool tx_nhalf = (used < (s->fifo_capacity / 2));
    set_ctrl_bit(s, SPI_CTRL_TX_NHALF, tx_nhalf);

    /* BUSY: We'll consider SPI busy if TX FIFO is not empty or currently shifting data.
     * For simplicity, if TX is not empty we say busy.
     */
    bool busy = !fifo8_is_empty(&s->tx_fifo);
    set_ctrl_bit(s, SPI_CTRL_BUSY, busy);
}

/* Update chip selects according to CS_SEL bits and CS_EN */
static void neorv32_spi_update_cs(NEORV32SPIState *s)
{
    if (s->cs_lines && s->num_cs > 0) {
        /* Determine which CS line is selected */
        int cs_index = 0;
        if (get_ctrl_bit(s, SPI_CTRL_CS_SEL1)) {
            cs_index = 1;
        }
        if (get_ctrl_bit(s, SPI_CTRL_CS_SEL2)) {
            cs_index = 2;
        }
        /* If multiple bits are set, last one takes precedence. Adjust logic as needed. */

        /* If CS_EN is set, the selected line is active (low) */
        bool cs_active = get_ctrl_bit(s, SPI_CTRL_CS_EN);

        /* Deactivate all lines first */
        for (int i = 0; i < s->num_cs; i++) {
            qemu_set_irq(s->cs_lines[i], 1);  // inactive (high)
        }
        if (cs_index < s->num_cs && cs_active) {
            qemu_set_irq(s->cs_lines[cs_index], 0); // active (low)
        }

//        flash_cs = qdev_get_gpio_in_named(flash_dev, SSI_GPIO_CS, 0);
//        sysbus_connect_irq(SYS_BUS_DEVICE(&s->soc.spi0), 1, flash_cs);
    }
}

/* Update IRQ based on conditions */
static void neorv32_spi_update_irq(NEORV32SPIState *s)
{
    /* Conditions for IRQ:
     * IRQ if RX data available and IRQ_RX_AVAIL is set:
     *    if (!RX FIFO empty && SPI_CTRL_IRQ_RX_AVAIL set)
     *
     * IRQ if TX empty and IRQ_TX_EMPTY is set:
     *    if (TX empty && SPI_CTRL_IRQ_TX_EMPTY set)
     *
     * IRQ if TX < half full and IRQ_TX_HALF is set:
     *    if (TX < half full && SPI_CTRL_IRQ_TX_HALF set)
     */

    bool rx_irq = get_ctrl_bit(s, SPI_CTRL_IRQ_RX_AVAIL) && !fifo8_is_empty(&s->rx_fifo);
    bool tx_empty_irq = get_ctrl_bit(s, SPI_CTRL_IRQ_TX_EMPTY) && fifo8_is_empty(&s->tx_fifo);
    int used = fifo8_num_used(&s->tx_fifo);
    bool tx_half_irq = get_ctrl_bit(s, SPI_CTRL_IRQ_TX_HALF) && (used < (s->fifo_capacity / 2));

    bool irq_level = rx_irq || tx_empty_irq || tx_half_irq;
    qemu_set_irq(s->irq, irq_level ? 1 : 0);
}

/* Flush the TX FIFO to the SPI bus:
 * For each byte in TX FIFO, send it out via ssi_transfer.
 * If direction is not explicitly given, we assume:
 *   - On write to DATA, we push to TX FIFO and then transfer out.
 *   - On receiving data back from ssi_transfer, we push it into RX FIFO
 *     if SPI is enabled.
 */
static void neorv32_spi_flush_txfifo(NEORV32SPIState *s)
{
    if (!get_ctrl_bit(s, SPI_CTRL_EN)) {
        /* SPI not enabled, do nothing */
        return;
    }

    while (!fifo8_is_empty(&s->tx_fifo)) {
        uint8_t tx = fifo8_pop(&s->tx_fifo);
        uint8_t rx = ssi_transfer(s->bus, tx);

        /* Push received byte into RX FIFO if not full */
        if (!fifo8_is_full(&s->rx_fifo)) {
            fifo8_push(&s->rx_fifo, rx);
        }
    }
}

/* Reset the device state */
static void neorv32_spi_reset(DeviceState *d)
{
    NEORV32SPIState *s = NEORV32_SPI(d);

    s->ctrl = 0;
    s->data = 0;

    /* Reset FIFOs */
    fifo8_reset(&s->tx_fifo);
    fifo8_reset(&s->rx_fifo);

    neorv32_spi_update_status(s);
    neorv32_spi_update_cs(s);
    neorv32_spi_update_irq(s);
}

/* MMIO read handler */
static uint64_t neorv32_spi_read(void *opaque, hwaddr addr, unsigned int size)
{
    NEORV32SPIState *s = opaque;
    uint32_t r = 0;

    switch (addr) {
    case NEORV32_SPI_CTRL:
        /* Return the current CTRL register value (including status bits) */
        neorv32_spi_update_status(s);
        r = s->ctrl;
        break;

    case NEORV32_SPI_DATA:
        /* If RX FIFO is empty, return some default, else pop from RX FIFO */
        if (fifo8_is_empty(&s->rx_fifo)) {
            /* No data available, could return 0xFFFFFFFF or 0x00000000 as "no data" */
            r = 0x00000000;
        } else {
            r = fifo8_pop(&s->rx_fifo);
        }
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read at address 0x%"
                       HWADDR_PRIx "\n", __func__, addr);
        break;
    }

    neorv32_spi_update_status(s);
    neorv32_spi_update_irq(s);

    return r;
}

/* MMIO write handler */
static void neorv32_spi_write(void *opaque, hwaddr addr,
                              uint64_t val64, unsigned int size)
{
    NEORV32SPIState *s = opaque;
    uint32_t value = val64;

    switch (addr) {
    case NEORV32_SPI_CTRL: {

        /* Writing control register:
         * Some bits are read-only (e.g., status bits).
         * We should mask them out or ignore writes to them.
         * For simplicity, we overwrite ctrl except for RO bits.
         */

        /* Save old RO bits: RX_AVAIL, TX_EMPTY, TX_NHALF, TX_FULL, BUSY and FIFO size bits */
        uint32_t ro_mask = ((1 << SPI_CTRL_BUSY)      |
                            (1 << SPI_CTRL_TX_EMPTY)  |
                            (1 << SPI_CTRL_TX_FULL)   |
                            (1 << SPI_CTRL_RX_AVAIL)  |
                            (1 << SPI_CTRL_TX_NHALF));

        /* FIFO size bits might be hardwired read-only. Assume we do not change them:
         * FIFO size: bits [SPI_CTRL_FIFO_LSB..SPI_CTRL_FIFO_MSB], here assume read-only.
         */
        uint32_t fifo_size_mask = 0;
        for (int b = SPI_CTRL_FIFO_LSB; b <= SPI_CTRL_FIFO_MSB; b++) {
            fifo_size_mask |= (1 << b);
        }
        ro_mask |= fifo_size_mask;

        uint32_t ro_bits = s->ctrl & ro_mask;
        s->ctrl = (value & ~ro_mask) | ro_bits;

        neorv32_spi_update_cs(s);
        break;
    }

    case NEORV32_SPI_DATA:
		{
			//Debug
			volatile int a = 0;
			if (value == 0x4) {
				a += 1;
			}else if (value == 0x6) {
				a += 2;
			}
		}
        /* Writing DATA puts a byte into TX FIFO if not full */
        if (!fifo8_is_full(&s->tx_fifo)) {
            uint8_t tx_byte = (uint8_t)value;

            /* Intercept the 0xAB opcode. */
             if (tx_byte == 0xAB) {
                 /* Option A: Replace it with a harmless NOP (e.g. 0x00 or 0xFF). */
                 tx_byte = 0x00;

                 /* Option B: Drop it entirely (don’t push to FIFO).
                    But that might break protocol timing from the guest’s perspective,
                    so usually better to push something valid, just not 0xAB. */
             }

            fifo8_push(&s->tx_fifo, tx_byte);
            /* After pushing data, flush TX to SPI bus */
            neorv32_spi_flush_txfifo(s);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: TX FIFO full, cannot write 0x%x\n",
                          __func__, value);
        }
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write at address 0x%"
                      HWADDR_PRIx " value=0x%x\n", __func__, addr, value);
        break;
    }

    neorv32_spi_update_status(s);
    neorv32_spi_update_irq(s);
}

static const MemoryRegionOps neorv32_spi_ops = {
    .read = neorv32_spi_read,
    .write = neorv32_spi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void neorv32_spi_init(Object *obj)
{
    NEORV32SPIState *s = NEORV32_SPI(obj);
    s->ctrl = 0;
    s->data = 0;
    s->fifo_capacity = 8; /* FIFO capacity of 8 bytes */
//s->last_rx = 0xFF;
    //s->cs_index = 0; /* Use CS0 by default */
    s->num_cs = 1; /* Default to 1 CS line */
}

/* Realize the device */
static void neorv32_spi_realize(DeviceState *dev, Error **errp)
{
    NEORV32SPIState *s = NEORV32_SPI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);

    /* Create SSI bus */
    s->bus = ssi_create_bus(dev, "neorv32-spi-bus");

    /* Initialize MMIO */
    memory_region_init_io(&s->mmio, OBJECT(s), &neorv32_spi_ops, s,
                          TYPE_NEORV32_SPI, NEORV32_SPI_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);

    /* Initialize interrupt line */
    sysbus_init_irq(sbd, &s->irq);

    /* s->num_cs is assigned at neorv32_spi_properties */
    s->cs_lines = g_new0(qemu_irq, s->num_cs);
    for (int i = 0; i < s->num_cs; i++) {
        sysbus_init_irq(sbd, &s->cs_lines[i]);
        /* Initially set CS high (inactive) */
        qemu_set_irq(s->cs_lines[i], 1);
    }


    /* Initialize FIFOs */
    fifo8_create(&s->tx_fifo, s->fifo_capacity);
    fifo8_create(&s->rx_fifo, s->fifo_capacity);

    /* Set FIFO size bits (log2 of FIFO size = 3 for capacity=8) */
    /* FIFO size bits: from SPI_CTRL_FIFO_LSB to SPI_CTRL_FIFO_MSB
     * We'll store a value of 3 (log2(8)=3)
     */
    int fifo_size_log2 = 3;
    for (int b = SPI_CTRL_FIFO_LSB; b <= SPI_CTRL_FIFO_MSB; b++) {
        int shift = b - SPI_CTRL_FIFO_LSB;
        if (fifo_size_log2 & (1 << shift)) {
            s->ctrl |= (1 << b);
        } else {
            s->ctrl &= ~(1 << b);
        }
    }
}

/* Device properties can be added if needed. For now, none. */
static Property neorv32_spi_properties[] = {
	DEFINE_PROP_UINT32("num-cs", NEORV32SPIState, num_cs, 1),
    DEFINE_PROP_END_OF_LIST(),
};

static void neorv32_spi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, neorv32_spi_properties);
    dc->reset = neorv32_spi_reset;
    dc->realize = neorv32_spi_realize;
}

static const TypeInfo neorv32_spi_type_info = {
    .name           = TYPE_NEORV32_SPI,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(NEORV32SPIState),
    .instance_init  = neorv32_spi_init,
    .class_init     = neorv32_spi_class_init,
};

static void neorv32_spi_register_types(void)
{
    type_register_static(&neorv32_spi_type_info);
}

type_init(neorv32_spi_register_types)

NEORV32SPIState *neorv32_spi_create(MemoryRegion *sys_mem, hwaddr base_addr)
{
    NEORV32SPIState *s = g_new0(NEORV32SPIState, 1);
    object_initialize(&s->parent_obj, sizeof(*s), TYPE_NEORV32_SPI);
    SysBusDevice *d = SYS_BUS_DEVICE(&s->parent_obj);

    /* Realize the device (this calls the realize function defined in neorv32_spi.c) */
    sysbus_realize_and_unref(d, &error_fatal);

    /* Map the device's MMIO region into the system address space */
    memory_region_add_subregion(sys_mem, base_addr, &s->mmio);


    //-------------NOT WORKS!!!!!!
//    DeviceState *flash_dev = DEVICE(&d->parent_obj);
//    if (flash_dev) {
//        //qemu_irq flash_cs = qdev_get_gpio_in_named(flash_dev, SSI_GPIO_CS, 0);
//        //sysbus_connect_irq(d, 1, flash_cs);
//		sysbus_connect_irq(d, 1, s->cs_lines[0]);
//    } else {
//    	error_report("Failed to find flash device with id=flash");
//    }
    //-------------END OF NOT WORKS!!!!!!

    return s;
}

