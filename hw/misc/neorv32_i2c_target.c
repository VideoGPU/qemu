/*
 * NEORV32 XBUS multi-address I2C target with clock stretching
 *
 * Models i2c_target_ip (videogpu:user:i2c_target_axi_ip:1.0, AXI4-Lite on
 * the NEORV32 external bus): register map, event FIFO (32), TX FIFO (16),
 * eight address slots, the TXREQ / ACK_CTRL protocol and the stretch
 * watchdog, as documented in the IP repository README.
 *
 * Clock stretching: the I2C side is a child I2CSlave on the TWD's "i2c" bus
 * implementing TYPE_I2C_TARGET_STRETCH. A stretch-aware master (the
 * i2c-master-chardev) waits while the target holds SCL low, so the guest
 * firmware runs and answers exactly like it does on the FPGA:
 *   before a read byte  TX FIFO empty -> one TXREQ event, stretch until data
 *   before a write byte fewer than 4 free event entries -> stretch
 *   ACK slot            ACK_CTRL mode -> stretch until TX_CTRL ACK / NACK
 * Not modelled: the glitch filter and SDA hold / setup timing (the registers
 * hold their values), the hardware's repeated START to another device (the
 * QEMU I2C core keeps the matched target; the chardev master issues STOP +
 * START instead).
 *
 * Copyright (c) 2026 VideoGPU
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "hw/i2c/i2c.h"
#include "hw/i2c/i2c_stretch.h"
#include "hw/misc/neorv32_i2c_target.h"
#include "monitor/qdev.h"

/* register offsets */
enum {
    R_ID              = 0x00,
    R_VERSION         = 0x04,
    R_CONFIG          = 0x08,
    R_CTRL            = 0x0C,
    R_STATUS          = 0x10,
    R_IRQ_STATUS      = 0x14,
    R_IRQ_ENABLE      = 0x18,
    R_EVENT           = 0x1C,
    R_TX_DATA         = 0x20,
    R_TX_CTRL         = 0x24,
    R_FILTER_CYCLES   = 0x28,
    R_SDA_HOLD_CYCLES = 0x2C,
    R_SETUP_CYCLES    = 0x30,
    R_STRETCH_TIMEOUT = 0x34,
    R_STRETCH_MAX     = 0x38,
    R_SLOT0           = 0x40,
};

#define ID_VALUE        0x49324354u
#define VERSION_VALUE   0x00010000u

#define CTRL_EN         BIT(0)
#define CTRL_ACK_CTRL   BIT(1)
#define CTRL_SOFT_RESET BIT(2)

#define TXC_FLUSH       BIT(0)
#define TXC_ACK         BIT(1)
#define TXC_NACK        BIT(2)

#define IRQ_EVT         BIT(0)
#define IRQ_WDOG        BIT(1)
#define IRQ_TX_OVERFLOW BIT(2)
#define IRQ_EVT_DROP    BIT(3)

enum { EV_START = 1, EV_RX = 2, EV_TXREQ = 3, EV_STOP = 4, EV_WDOG = 5 };

#define CLK_NS          10      /* s_axi_aclk = 100 MHz */

#define TYPE_NEORV32_I2C_TARGET_SLAVE "neorv32.i2c-target-slave"
OBJECT_DECLARE_SIMPLE_TYPE(Neorv32I2CTargetSlave, NEORV32_I2C_TARGET_SLAVE)

typedef struct Neorv32I2CTargetSlave {
    I2CSlave parent_obj;
    Neorv32I2CTargetState *dev;
} Neorv32I2CTargetSlave;

static uint32_t make_event(int type, int slot, bool rw, bool repeated,
                           bool need_ack, int flushed, uint8_t data)
{
    return ((uint32_t)type << 24) | ((uint32_t)(slot & 7) << 16) |
           ((uint32_t)rw << 15) | ((uint32_t)repeated << 14) |
           ((uint32_t)need_ack << 13) | ((uint32_t)(flushed & 0x1F) << 8) | data;
}

static bool enabled(Neorv32I2CTargetState *s)
{
    return s->ctrl & CTRL_EN;
}

static uint32_t evt_free(Neorv32I2CTargetState *s)
{
    return fifo32_num_free(&s->evt);
}

static void update_irq(Neorv32I2CTargetState *s)
{
    uint32_t status = s->irq_sticky | (fifo32_is_empty(&s->evt) ? 0 : IRQ_EVT);

    qemu_set_irq(s->irq, enabled(s) && (status & s->irq_enable & 0xF));
}

/* normal events need 4 free entries (the hardware stretches before) */
static void push_event(Neorv32I2CTargetState *s, uint32_t ev, bool normal)
{
    if (fifo32_is_full(&s->evt) || (normal && s->wd_tripped && evt_free(s) < 4)) {
        s->irq_sticky |= IRQ_EVT_DROP;
    } else {
        fifo32_push(&s->evt, ev);
    }
    update_irq(s);
}

static void stretch_begin(Neorv32I2CTargetState *s, int point)
{
    if (s->stretch_start < 0) {
        s->stretch_start = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        s->stretch_point = point;
        if (s->stretch_timeout) {
            timer_mod(&s->wdog_timer,
                      s->stretch_start + (int64_t)s->stretch_timeout * CLK_NS);
        }
    }
}

static void stretch_end(Neorv32I2CTargetState *s)
{
    if (s->stretch_start >= 0) {
        int64_t cycles = (qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) -
                          s->stretch_start) / CLK_NS;

        if (cycles > s->stretch_max) {
            s->stretch_max = cycles > UINT32_MAX ? UINT32_MAX : (uint32_t)cycles;
        }
        s->stretch_start = -1;
        timer_del(&s->wdog_timer);
    }
}

static void wdog_expired(void *opaque)
{
    Neorv32I2CTargetState *s = opaque;

    if (s->stretch_start < 0 || s->wd_tripped) {
        return;
    }
    s->wd_tripped = true;
    s->irq_sticky |= IRQ_WDOG;
    stretch_end(s);
    push_event(s, make_event(EV_WDOG, s->cur_slot, s->rw, false, false, 0,
                             (uint8_t)s->stretch_point), false);
}

static void core_reset(Neorv32I2CTargetState *s)
{
    fifo32_reset(&s->evt);
    fifo8_reset(&s->tx);
    s->in_transfer = false;
    s->txreq_sent = false;
    s->wd_tripped = false;
    s->rx_wait_ack = false;
    s->nack_given = false;
    s->ack_dec_valid = false;
    s->stretch_start = -1;
    timer_del(&s->wdog_timer);
    update_irq(s);
}

/* ------------------------------------------------------------ I2C side */

static bool target_match(I2CSlave *candidate, uint8_t address, bool broadcast,
                         I2CNodeList *current_devs)
{
    Neorv32I2CTargetState *s = NEORV32_I2C_TARGET_SLAVE(candidate)->dev;
    int i;

    if (!enabled(s) || broadcast) {
        return false;
    }
    for (i = 0; i < NEORV32_I2C_TARGET_SLOTS; i++) {
        if ((s->slot[i] & BIT(31)) && (s->slot[i] & 0x7F) == address) {
            I2CNode *node = g_new(struct I2CNode, 1);

            s->match_slot = i;
            node->elt = candidate;
            QLIST_INSERT_HEAD(current_devs, node, next);
            return true;
        }
    }
    return false;
}

static int target_event(I2CSlave *i2c, enum i2c_event event)
{
    Neorv32I2CTargetState *s = NEORV32_I2C_TARGET_SLAVE(i2c)->dev;
    int flushed;

    if (!enabled(s)) {
        return 1;
    }
    switch (event) {
    case I2C_START_SEND:
    case I2C_START_SEND_ASYNC:
    case I2C_START_RECV:
        /* every START empties the TX FIFO (late bytes never leak) */
        flushed = s->in_transfer ? fifo8_num_used(&s->tx) : 0;
        fifo8_reset(&s->tx);
        s->rw = event == I2C_START_RECV;
        s->cur_slot = s->match_slot;
        s->txreq_sent = false;
        s->wd_tripped = false;
        s->rx_wait_ack = false;
        s->nack_given = false;
        s->ack_dec_valid = false;
        stretch_end(s);
        push_event(s, make_event(EV_START, s->cur_slot, s->rw, s->in_transfer,
                                 false, flushed, s->slot[s->cur_slot] & 0x7F), true);
        s->in_transfer = true;
        break;
    case I2C_FINISH:
        if (s->in_transfer) {
            push_event(s, make_event(EV_STOP, s->cur_slot, s->rw, false, false,
                                     fifo8_num_used(&s->tx), 0), false);
            fifo8_reset(&s->tx);
        }
        s->in_transfer = false;
        s->wd_tripped = false;
        stretch_end(s);
        break;
    default:
        break;
    }
    return 0;
}

static int target_send(I2CSlave *i2c, uint8_t data)
{
    Neorv32I2CTargetState *s = NEORV32_I2C_TARGET_SLAVE(i2c)->dev;
    bool need_ack;

    if (!enabled(s) || !s->in_transfer || s->nack_given) {
        return 1;
    }
    need_ack = (s->ctrl & CTRL_ACK_CTRL) && !s->wd_tripped;
    push_event(s, make_event(EV_RX, s->cur_slot, s->rw, false, need_ack, 0, data),
               true);
    s->rx_wait_ack = need_ack;
    return 0;
}

static uint8_t target_recv(I2CSlave *i2c)
{
    Neorv32I2CTargetState *s = NEORV32_I2C_TARGET_SLAVE(i2c)->dev;
    uint8_t v = 0xFF;

    if (enabled(s) && !fifo8_is_empty(&s->tx)) {
        v = fifo8_pop(&s->tx);
    }
    s->txreq_sent = false;
    stretch_end(s);
    return v;
}

static bool target_stretch(I2CSlave *i2c, I2CStretchPoint point)
{
    Neorv32I2CTargetState *s = NEORV32_I2C_TARGET_SLAVE(i2c)->dev;

    if (!enabled(s) || !s->in_transfer || s->wd_tripped) {
        stretch_end(s);
        s->rx_wait_ack = false;
        return false;
    }
    switch (point) {
    case I2C_STRETCH_BEFORE_SEND:
        if (evt_free(s) < 4) {
            stretch_begin(s, point);
            return true;
        }
        break;
    case I2C_STRETCH_AFTER_SEND:
        if (s->rx_wait_ack) {
            if (!s->ack_dec_valid) {
                stretch_begin(s, point);
                return true;
            }
            s->nack_given = s->ack_dec_nack;
            s->ack_dec_valid = false;
            s->rx_wait_ack = false;
        }
        break;
    case I2C_STRETCH_BEFORE_RECV:
        if (fifo8_is_empty(&s->tx)) {
            if (!s->txreq_sent && evt_free(s) >= 4) {
                push_event(s, make_event(EV_TXREQ, s->cur_slot, s->rw, false,
                                         false, 0, 0), true);
                s->txreq_sent = true;
            }
            stretch_begin(s, point);
            return true;
        }
        break;
    }
    stretch_end(s);
    return false;
}

static bool target_nacked(I2CSlave *i2c)
{
    return NEORV32_I2C_TARGET_SLAVE(i2c)->dev->nack_given;
}

static void target_slave_class_init(ObjectClass *oc, const void *data)
{
    I2CSlaveClass *k = I2C_SLAVE_CLASS(oc);
    I2CTargetStretchClass *sk = I2C_TARGET_STRETCH_CLASS(oc);

    k->match_and_add = target_match;
    k->event = target_event;
    k->send = target_send;
    k->recv = target_recv;
    sk->stretch = target_stretch;
    sk->nacked = target_nacked;
}

static const TypeInfo target_slave_info = {
    .name          = TYPE_NEORV32_I2C_TARGET_SLAVE,
    .parent        = TYPE_I2C_SLAVE,
    .instance_size = sizeof(Neorv32I2CTargetSlave),
    .class_init    = target_slave_class_init,
    .interfaces    = (const InterfaceInfo[]) {
        { TYPE_I2C_TARGET_STRETCH },
        { }
    },
};

/* ------------------------------------------------------------ registers */

static uint64_t neorv32_i2c_target_read(void *opaque, hwaddr addr, unsigned size)
{
    Neorv32I2CTargetState *s = opaque;
    uint32_t v = 0;
    bool stretching = s->stretch_start >= 0;

    switch (addr) {
    case R_ID:
        v = ID_VALUE;
        break;
    case R_VERSION:
        v = VERSION_VALUE;
        break;
    case R_CONFIG:
        v = (NEORV32_I2C_TARGET_TX_DEPTH << 16) |
            (NEORV32_I2C_TARGET_EVT_DEPTH << 8) | NEORV32_I2C_TARGET_SLOTS;
        break;
    case R_CTRL:
        v = s->ctrl;
        break;
    case R_STATUS:
        v = ((uint32_t)fifo8_num_used(&s->tx) << 24) |
            ((uint32_t)fifo32_num_used(&s->evt) << 16) |
            ((uint32_t)(stretching && s->rx_wait_ack) << 9) |
            ((uint32_t)(stretching && s->stretch_point == I2C_STRETCH_BEFORE_RECV) << 8) |
            ((uint32_t)stretching << 7) |
            ((uint32_t)(s->cur_slot & 7) << 4) |
            ((uint32_t)s->rw << 2) |
            ((uint32_t)s->in_transfer << 1) |
            (uint32_t)s->in_transfer;
        break;
    case R_IRQ_STATUS:
        v = s->irq_sticky | (fifo32_is_empty(&s->evt) ? 0 : IRQ_EVT);
        break;
    case R_IRQ_ENABLE:
        v = s->irq_enable;
        break;
    case R_EVENT:
        if (!fifo32_is_empty(&s->evt)) {
            v = fifo32_pop(&s->evt) | BIT(31);
            update_irq(s);
        }
        break;
    case R_FILTER_CYCLES:
        v = s->filter_cycles;
        break;
    case R_SDA_HOLD_CYCLES:
        v = s->sda_hold_cycles;
        break;
    case R_SETUP_CYCLES:
        v = s->setup_cycles;
        break;
    case R_STRETCH_TIMEOUT:
        v = s->stretch_timeout;
        break;
    case R_STRETCH_MAX:
        v = s->stretch_max;
        break;
    default:
        if (addr >= R_SLOT0 && addr < R_SLOT0 + 4 * NEORV32_I2C_TARGET_SLOTS &&
            !(addr & 3)) {
            v = s->slot[(addr - R_SLOT0) / 4];
        }
        break;
    }
    return v;
}

static void neorv32_i2c_target_write(void *opaque, hwaddr addr, uint64_t val64,
                                     unsigned size)
{
    Neorv32I2CTargetState *s = opaque;
    uint32_t val = val64;

    switch (addr) {
    case R_CTRL:
        s->ctrl = val & (CTRL_EN | CTRL_ACK_CTRL);
        if ((val & CTRL_SOFT_RESET) || !enabled(s)) {
            core_reset(s);
        }
        break;
    case R_IRQ_STATUS:
        s->irq_sticky &= ~(val & (IRQ_WDOG | IRQ_TX_OVERFLOW | IRQ_EVT_DROP));
        break;
    case R_IRQ_ENABLE:
        s->irq_enable = val & 0xF;
        break;
    case R_TX_DATA:
        if (enabled(s)) {
            if (fifo8_is_full(&s->tx)) {
                s->irq_sticky |= IRQ_TX_OVERFLOW;
            } else {
                fifo8_push(&s->tx, val & 0xFF);
            }
        }
        break;
    case R_TX_CTRL:
        if (val & TXC_FLUSH) {
            fifo8_reset(&s->tx);
        }
        if (enabled(s) && (val & (TXC_ACK | TXC_NACK))) {
            s->ack_dec_valid = true;
            s->ack_dec_nack = val & TXC_NACK;
        }
        break;
    case R_FILTER_CYCLES:
        s->filter_cycles = val & 0xFF;
        break;
    case R_SDA_HOLD_CYCLES:
        s->sda_hold_cycles = val & 0xFF;
        break;
    case R_SETUP_CYCLES:
        s->setup_cycles = val & 0xFF;
        break;
    case R_STRETCH_TIMEOUT:
        s->stretch_timeout = val;
        break;
    case R_STRETCH_MAX:
        s->stretch_max = 0;
        break;
    default:
        if (addr >= R_SLOT0 && addr < R_SLOT0 + 4 * NEORV32_I2C_TARGET_SLOTS &&
            !(addr & 3)) {
            s->slot[(addr - R_SLOT0) / 4] = val & 0x8000007F;
        } else if (addr >= 0x100 || addr == R_ID || addr == R_VERSION ||
                   addr == R_CONFIG || addr == R_STATUS || addr == R_EVENT) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: write to 0x%" HWADDR_PRIx " ignored\n",
                          __func__, addr);
        }
        break;
    }
    update_irq(s);
}

static const MemoryRegionOps neorv32_i2c_target_ops = {
    .read = neorv32_i2c_target_read,
    .write = neorv32_i2c_target_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void neorv32_i2c_target_reset(DeviceState *dev)
{
    Neorv32I2CTargetState *s = NEORV32_I2C_TARGET(dev);

    s->ctrl = 0;
    s->irq_enable = 0;
    s->irq_sticky = 0;
    s->filter_cycles = 5;
    s->sda_hold_cycles = 30;
    s->setup_cycles = 10;
    s->stretch_timeout = 1000000;
    s->stretch_max = 0;
    memset(s->slot, 0, sizeof(s->slot));
    core_reset(s);
}

static void neorv32_i2c_target_init(Object *obj)
{
    Neorv32I2CTargetState *s = NEORV32_I2C_TARGET(obj);

    memory_region_init_io(&s->mmio, obj, &neorv32_i2c_target_ops, s,
                          TYPE_NEORV32_I2C_TARGET, NEORV32_I2C_TARGET_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    fifo32_create(&s->evt, NEORV32_I2C_TARGET_EVT_DEPTH);
    fifo8_create(&s->tx, NEORV32_I2C_TARGET_TX_DEPTH);
    timer_init_ns(&s->wdog_timer, QEMU_CLOCK_VIRTUAL, wdog_expired, s);
    s->stretch_start = -1;
}

static void neorv32_i2c_target_finalize(Object *obj)
{
    Neorv32I2CTargetState *s = NEORV32_I2C_TARGET(obj);

    timer_del(&s->wdog_timer);
    fifo32_destroy(&s->evt);
    fifo8_destroy(&s->tx);
}

static void neorv32_i2c_target_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, neorv32_i2c_target_reset);
    dc->user_creatable = false;
}

static const TypeInfo neorv32_i2c_target_info = {
    .name              = TYPE_NEORV32_I2C_TARGET,
    .parent            = TYPE_SYS_BUS_DEVICE,
    .instance_size     = sizeof(Neorv32I2CTargetState),
    .instance_init     = neorv32_i2c_target_init,
    .instance_finalize = neorv32_i2c_target_finalize,
    .class_init        = neorv32_i2c_target_class_init,
};

static void neorv32_i2c_target_register_types(void)
{
    type_register_static(&neorv32_i2c_target_info);
    type_register_static(&target_slave_info);
}

type_init(neorv32_i2c_target_register_types)

Neorv32I2CTargetState *neorv32_i2c_target_create(MemoryRegion *address_space,
                                                 hwaddr base, I2CBus *bus)
{
    DeviceState *dev = qdev_new(TYPE_NEORV32_I2C_TARGET);
    Neorv32I2CTargetState *s = NEORV32_I2C_TARGET(dev);
    DeviceState *slave;

    qdev_set_id(dev, g_strdup("i2c-target"), &error_fatal);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    memory_region_add_subregion(address_space, base,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0));

    slave = qdev_new(TYPE_NEORV32_I2C_TARGET_SLAVE);
    NEORV32_I2C_TARGET_SLAVE(slave)->dev = s;
    /* the address property is unused: target_match() checks the slots */
    i2c_slave_set_address(I2C_SLAVE(slave), 0x7F);
    i2c_slave_realize_and_unref(I2C_SLAVE(slave), bus, &error_fatal);
    s->slave = I2C_SLAVE(slave);
    return s;
}
