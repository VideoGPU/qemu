/*
 * MIPI CSI-2 TX AXI4-Lite IP attached to the NEORV32 external bus (XBUS)
 *
 * Models the register interface and frame timing of mipi_tx_axi_ip.sv. The
 * D-PHY line coding is not modelled; the optional "dumpfile" property writes
 * the generated CSI-2 packet stream (short packets, long packets, header ECC
 * and payload CRC) so it can be fed to the mipi_decoder tooling.
 *
 * Copyright (c) 2026 Michael Levit
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "monitor/qdev.h"
#include "migration/vmstate.h"
#include "system/address-spaces.h"
#include "hw/misc/neorv32_mipi_tx.h"
#include "trace.h"

/* Register offsets, must match the AXI4-Lite map of mipi_tx_axi_ip.sv */
enum {
    REG_CONTROL            = 0x00,
    REG_STATUS             = 0x04,
    REG_PIXELS_PER_LINE    = 0x10,
    REG_N_LINES            = 0x14,
    REG_TYPE_VC            = 0x18,
    REG_FRAME_END_WORD     = 0x1c,
    REG_TLP_SOT_DELAY_CLK  = 0x20,
    REG_TLPX_DELAY_CLK     = 0x24,
    REG_TLP_SOT_DELAY_DATA = 0x28,
    REG_TLPX_DELAY_DATA    = 0x2c,
    REG_TLP_SOT_SHORT      = 0x30,
    REG_THS_PREPARE        = 0x34,
    REG_THS_ZERO           = 0x38,
    REG_THS_EXIT           = 0x3c,
    REG_N_MIPI_LANES       = 0x40,
};

/* CONTROL register bits */
#define CTRL_START        0
#define CTRL_STOP         1
#define CTRL_IRQ_EN       2
#define CTRL_LEGACY_TRIG  3
#define CTRL_IRQ_CLR      4
#define CTRL_OVERLAY_EN   5

/* Bits kept in the readable part of CONTROL (start and irq-clear self-clear) */
#define CTRL_PERSIST_MASK (BIT(CTRL_STOP) | BIT(CTRL_IRQ_EN) | \
                           BIT(CTRL_LEGACY_TRIG) | BIT(CTRL_OVERLAY_EN))

/* STATUS register bits */
#define STATUS_HS_ACTIVE  0
#define STATUS_HS_VALID   1
#define STATUS_FRAME_DONE 2
#define STATUS_IRQ_PEND   3

/* Reset values, must match CFG_RESET in mipi_tx_axi_ip.sv */
#define RESET_CONTROL            0x00000028
#define RESET_PIXELS_PER_LINE    3240
#define RESET_N_LINES            1944
#define RESET_TYPE_VC            0x0000002b
#define RESET_FRAME_END_WORD     0x1753
#define RESET_TLP_SOT_DELAY_CLK  42
#define RESET_TLPX_DELAY_CLK     10
#define RESET_TLP_SOT_DELAY_DATA 187
#define RESET_TLPX_DELAY_DATA    10
#define RESET_TLP_SOT_SHORT      783
#define RESET_THS_PREPARE        15
#define RESET_THS_ZERO           80
#define RESET_THS_EXIT           10
#define RESET_N_MIPI_LANES       2

/* CSI-2 data types */
#define DT_FRAME_START 0x00
#define DT_FRAME_END   0x01
#define DT_RAW8        0x2a
#define DT_RAW10       0x2b
#define DT_RAW12       0x2c

#define CSI2_HEADER_BYTES 4
#define CSI2_CRC_BYTES    2

/* Colorbar generator constants, see colorbar_generator.sv / common_pkg.sv */
#define COLORBAR_BANDS            8
#define DBG_OVERLAY_SHIFT_PER_FRAME 10

/* One D-PHY tick of the IP's 100MHz clock */
#define DPHY_TICK_NS 10

static uint32_t mipi_tx_lane_count(const Neorv32MipiTxState *s)
{
    switch (s->n_mipi_lanes) {
    case 1:  return 1;
    case 3:  return 4;
    default: return 2;
    }
}

static uint32_t mipi_tx_data_type(const Neorv32MipiTxState *s)
{
    return s->type_vc & 0x3f;
}

static uint32_t mipi_tx_vc(const Neorv32MipiTxState *s)
{
    return (s->type_vc >> 8) & 0x03;
}

static uint32_t mipi_tx_bits_per_pixel(const Neorv32MipiTxState *s)
{
    switch (mipi_tx_data_type(s)) {
    case DT_RAW8:  return 8;
    case DT_RAW12: return 12;
    default:       return 10;
    }
}

static void neorv32_mipi_tx_update_irq(Neorv32MipiTxState *s)
{
    bool level = s->irq_pending && (s->control & BIT(CTRL_IRQ_EN));

    qemu_set_irq(s->irq, level);
}

/*
 * Frame duration derived from the programmed geometry, lane count and D-PHY
 * timing registers. Each LP->HS entry costs the configured delays, each byte
 * on a lane costs one tick of the byte clock.
 */
static uint64_t neorv32_mipi_tx_frame_ns(const Neorv32MipiTxState *s)
{
    uint64_t lanes = mipi_tx_lane_count(s);
    uint64_t entry = (uint64_t)s->tlp_sot_delay_data + s->tlpx_delay_data +
                     s->ths_prepare + s->ths_zero + s->ths_exit;
    uint64_t short_pkt = entry + s->tlp_sot_short +
                         DIV_ROUND_UP(CSI2_HEADER_BYTES, lanes);
    uint64_t line_bytes = (uint64_t)s->pixels_per_line +
                          CSI2_HEADER_BYTES + CSI2_CRC_BYTES;
    uint64_t ticks;

    ticks = (uint64_t)s->tlp_sot_delay_clk + s->tlpx_delay_clk;
    ticks += 2 * short_pkt;
    ticks += (uint64_t)s->n_lines * (entry + DIV_ROUND_UP(line_bytes, lanes));

    return ticks * DPHY_TICK_NS;
}

static uint8_t neorv32_mipi_tx_header_ecc(uint32_t header24)
{
    static const uint32_t parity_mask[6] = {
        0x00f12cb7, 0x00f2555b, 0x00749a6d,
        0x00b8e38e, 0x00df03f0, 0x00effc00,
    };
    uint8_t ecc = 0;
    int i;

    for (i = 0; i < 6; i++) {
        ecc |= (uint8_t)(ctpop32(header24 & parity_mask[i]) & 1) << i;
    }

    return ecc;
}

static uint16_t neorv32_mipi_tx_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xffff;
    size_t i;
    int b;

    for (i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                                 : (uint16_t)(crc << 1);
        }
    }

    return crc;
}

static void neorv32_mipi_tx_dump_header(Neorv32MipiTxState *s, uint32_t dt,
                                        uint16_t word_count)
{
    uint8_t header[CSI2_HEADER_BYTES];
    uint32_t data_id = (mipi_tx_vc(s) << 6) | (dt & 0x3f);

    header[0] = (uint8_t)data_id;
    header[1] = (uint8_t)word_count;
    header[2] = (uint8_t)(word_count >> 8);
    header[3] = neorv32_mipi_tx_header_ecc(data_id | (word_count << 8));

    fwrite(header, 1, sizeof(header), s->dump);
}

/*
 * Reproduce the 75% colorbar of colorbar_generator.sv: R = ~idx[1],
 * G = ~idx[2], B = ~idx[0] over a BGGR Bayer grid. Pixel samples are
 * all-or-nothing, so a set channel is full scale.
 */
static uint32_t neorv32_mipi_tx_sample(const Neorv32MipiTxState *s,
                                       uint32_t n_pixels, uint32_t pix_per_band,
                                       uint32_t line, uint32_t pixel)
{
    uint32_t shifted = (n_pixels - s->overlay_offset + pixel) % n_pixels;
    uint32_t idx = MIN(shifted / pix_per_band, COLORBAR_BANDS - 1);
    bool red   = !((idx >> 1) & 1);
    bool green = !((idx >> 2) & 1);
    bool blue  = !(idx & 1);
    bool even_line = line & 1;
    bool on;

    if (even_line) {
        on = (pixel & 1) ? green : blue;
    } else {
        on = (pixel & 1) ? red : green;
    }

    return on ? (1u << mipi_tx_bits_per_pixel(s)) - 1 : 0;
}

static void neorv32_mipi_tx_build_line(const Neorv32MipiTxState *s,
                                       uint32_t line, uint8_t *payload,
                                       size_t len)
{
    uint32_t bpp = mipi_tx_bits_per_pixel(s);
    uint32_t n_pixels = (uint32_t)((len * 8) / bpp);
    uint32_t pix_per_band;
    uint32_t pixel = 0;
    size_t pos = 0;

    if (n_pixels < COLORBAR_BANDS) {
        memset(payload, 0, len);
        return;
    }
    pix_per_band = n_pixels / COLORBAR_BANDS;

    while (pos < len) {
        uint32_t p[4];
        uint8_t group[5];
        size_t group_len;
        size_t i;

        for (i = 0; i < ARRAY_SIZE(p); i++) {
            p[i] = neorv32_mipi_tx_sample(s, n_pixels, pix_per_band, line,
                                          (pixel + i) % n_pixels);
        }

        switch (bpp) {
        case 8:
            group[0] = (uint8_t)p[0];
            group_len = 1;
            pixel += 1;
            break;
        case 12:
            group[0] = (uint8_t)(p[0] >> 4);
            group[1] = (uint8_t)(p[1] >> 4);
            group[2] = (uint8_t)(((p[1] & 0xf) << 4) | (p[0] & 0xf));
            group_len = 3;
            pixel += 2;
            break;
        default:
            group[0] = (uint8_t)(p[0] >> 2);
            group[1] = (uint8_t)(p[1] >> 2);
            group[2] = (uint8_t)(p[2] >> 2);
            group[3] = (uint8_t)(p[3] >> 2);
            group[4] = (uint8_t)(((p[3] & 3) << 6) | ((p[2] & 3) << 4) |
                                 ((p[1] & 3) << 2) | (p[0] & 3));
            group_len = 5;
            pixel += 4;
            break;
        }

        for (i = 0; i < group_len && pos < len; i++) {
            payload[pos++] = group[i];
        }
        pixel %= n_pixels;
    }
}

static void neorv32_mipi_tx_dump_frame(Neorv32MipiTxState *s)
{
    uint16_t word_count = s->pixels_per_line;
    uint8_t *payload;
    uint32_t line;

    if (!s->dump) {
        return;
    }

    neorv32_mipi_tx_dump_header(s, DT_FRAME_START, s->frame_number);

    payload = g_malloc(word_count);
    for (line = 0; line < s->n_lines; line++) {
        uint8_t crc[CSI2_CRC_BYTES];
        uint16_t crc16;

        /* the RTL line counter starts at 1, which sets the Bayer phase */
        neorv32_mipi_tx_build_line(s, line + 1, payload, word_count);
        neorv32_mipi_tx_dump_header(s, mipi_tx_data_type(s), word_count);
        fwrite(payload, 1, word_count, s->dump);

        crc16 = neorv32_mipi_tx_crc16(payload, word_count);
        crc[0] = (uint8_t)crc16;
        crc[1] = (uint8_t)(crc16 >> 8);
        fwrite(crc, 1, sizeof(crc), s->dump);
    }
    g_free(payload);

    neorv32_mipi_tx_dump_header(s, DT_FRAME_END, s->frame_end_word);
    fflush(s->dump);
}

static void neorv32_mipi_tx_finish_frame(Neorv32MipiTxState *s)
{
    s->hs_active = false;
    s->hs_data_valid = false;
    s->frame_done = true;
    s->irq_pending = true;

    neorv32_mipi_tx_update_irq(s);
}

static void neorv32_mipi_tx_frame_timer(void *opaque)
{
    Neorv32MipiTxState *s = opaque;

    trace_neorv32_mipi_tx_frame_done(s->frame_number);
    neorv32_mipi_tx_dump_frame(s);
    neorv32_mipi_tx_finish_frame(s);
}

static void neorv32_mipi_tx_start_frame(Neorv32MipiTxState *s)
{
    uint64_t duration = neorv32_mipi_tx_frame_ns(s);

    if (s->pixels_per_line == 0 || s->n_lines == 0) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: start with empty geometry %ux%u\n", __func__,
                      s->pixels_per_line, s->n_lines);
        return;
    }

    /* the RTL bumps the frame counter and scrolls the overlay before the FS packet */
    s->frame_number++;
    if (s->control & BIT(CTRL_OVERLAY_EN)) {
        uint32_t n_pixels = ((uint32_t)s->pixels_per_line * 8) /
                            mipi_tx_bits_per_pixel(s);
        if (n_pixels) {
            s->overlay_offset =
                (s->overlay_offset + DBG_OVERLAY_SHIFT_PER_FRAME) % n_pixels;
        }
    } else {
        s->overlay_offset = 0;
    }

    trace_neorv32_mipi_tx_frame_start(s->pixels_per_line, s->n_lines,
                                      mipi_tx_lane_count(s), duration);

    s->hs_active = true;
    s->hs_data_valid = true;
    s->frame_done = false;
    timer_mod(&s->frame_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + duration);
}

static uint64_t neorv32_mipi_tx_read(void *opaque, hwaddr addr, unsigned size)
{
    Neorv32MipiTxState *s = opaque;
    uint32_t val;

    switch (addr) {
    case REG_CONTROL:
        val = s->control;
        break;
    case REG_STATUS:
        val = ((uint32_t)s->hs_active     << STATUS_HS_ACTIVE)  |
              ((uint32_t)s->hs_data_valid << STATUS_HS_VALID)   |
              ((uint32_t)s->frame_done    << STATUS_FRAME_DONE) |
              ((uint32_t)s->irq_pending   << STATUS_IRQ_PEND);
        break;
    case REG_PIXELS_PER_LINE:
        val = s->pixels_per_line;
        break;
    case REG_N_LINES:
        val = s->n_lines;
        break;
    case REG_TYPE_VC:
        val = s->type_vc;
        break;
    case REG_FRAME_END_WORD:
        val = s->frame_end_word;
        break;
    case REG_TLP_SOT_DELAY_CLK:
        val = s->tlp_sot_delay_clk;
        break;
    case REG_TLPX_DELAY_CLK:
        val = s->tlpx_delay_clk;
        break;
    case REG_TLP_SOT_DELAY_DATA:
        val = s->tlp_sot_delay_data;
        break;
    case REG_TLPX_DELAY_DATA:
        val = s->tlpx_delay_data;
        break;
    case REG_TLP_SOT_SHORT:
        val = s->tlp_sot_short;
        break;
    case REG_THS_PREPARE:
        val = s->ths_prepare;
        break;
    case REG_THS_ZERO:
        val = s->ths_zero;
        break;
    case REG_THS_EXIT:
        val = s->ths_exit;
        break;
    case REG_N_MIPI_LANES:
        val = s->n_mipi_lanes;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid read addr=0x%" HWADDR_PRIx " size=%u\n",
                      __func__, addr, size);
        return 0;
    }

    trace_neorv32_mipi_tx_read(addr, val);
    return val;
}

static void neorv32_mipi_tx_write(void *opaque, hwaddr addr, uint64_t val64,
                                  unsigned size)
{
    Neorv32MipiTxState *s = opaque;
    uint32_t val = (uint32_t)val64;

    trace_neorv32_mipi_tx_write(addr, val);

    switch (addr) {
    case REG_CONTROL:
        s->control = val & CTRL_PERSIST_MASK;
        if (val & BIT(CTRL_IRQ_CLR)) {
            s->irq_pending = false;
        }
        if (val & BIT(CTRL_START)) {
            neorv32_mipi_tx_start_frame(s);
        } else if ((val & BIT(CTRL_STOP)) && timer_pending(&s->frame_timer)) {
            timer_del(&s->frame_timer);
            neorv32_mipi_tx_finish_frame(s);
        }
        neorv32_mipi_tx_update_irq(s);
        break;
    case REG_PIXELS_PER_LINE:
        s->pixels_per_line = (uint16_t)val;
        break;
    case REG_N_LINES:
        s->n_lines = (uint16_t)val;
        break;
    case REG_TYPE_VC:
        s->type_vc = val & 0x33f;
        break;
    case REG_FRAME_END_WORD:
        s->frame_end_word = (uint16_t)val;
        break;
    case REG_TLP_SOT_DELAY_CLK:
        s->tlp_sot_delay_clk = (uint16_t)val;
        break;
    case REG_TLPX_DELAY_CLK:
        s->tlpx_delay_clk = (uint16_t)val;
        break;
    case REG_TLP_SOT_DELAY_DATA:
        s->tlp_sot_delay_data = (uint16_t)val;
        break;
    case REG_TLPX_DELAY_DATA:
        s->tlpx_delay_data = (uint16_t)val;
        break;
    case REG_TLP_SOT_SHORT:
        s->tlp_sot_short = (uint16_t)val;
        break;
    case REG_THS_PREPARE:
        s->ths_prepare = (uint16_t)val;
        break;
    case REG_THS_ZERO:
        s->ths_zero = (uint16_t)val;
        break;
    case REG_THS_EXIT:
        s->ths_exit = (uint16_t)val;
        break;
    case REG_N_MIPI_LANES:
        s->n_mipi_lanes = val & 0x3;
        break;
    case REG_STATUS:
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: invalid write addr=0x%" HWADDR_PRIx
                      " size=%u val=0x%x\n", __func__, addr, size, val);
        break;
    }
}

static const MemoryRegionOps neorv32_mipi_tx_ops = {
    .read = neorv32_mipi_tx_read,
    .write = neorv32_mipi_tx_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void neorv32_mipi_tx_reset(DeviceState *dev)
{
    Neorv32MipiTxState *s = NEORV32_MIPI_TX(dev);

    timer_del(&s->frame_timer);

    s->control            = RESET_CONTROL;
    s->pixels_per_line    = RESET_PIXELS_PER_LINE;
    s->n_lines            = RESET_N_LINES;
    s->type_vc            = RESET_TYPE_VC;
    s->frame_end_word     = RESET_FRAME_END_WORD;
    s->tlp_sot_delay_clk  = RESET_TLP_SOT_DELAY_CLK;
    s->tlpx_delay_clk     = RESET_TLPX_DELAY_CLK;
    s->tlp_sot_delay_data = RESET_TLP_SOT_DELAY_DATA;
    s->tlpx_delay_data    = RESET_TLPX_DELAY_DATA;
    s->tlp_sot_short      = RESET_TLP_SOT_SHORT;
    s->ths_prepare        = RESET_THS_PREPARE;
    s->ths_zero           = RESET_THS_ZERO;
    s->ths_exit           = RESET_THS_EXIT;
    s->n_mipi_lanes       = RESET_N_MIPI_LANES;

    s->hs_active      = false;
    s->hs_data_valid  = false;
    s->frame_done     = false;
    s->irq_pending    = false;
    s->frame_number   = 0;
    s->overlay_offset = 0;

    neorv32_mipi_tx_update_irq(s);
}

static void neorv32_mipi_tx_init(Object *obj)
{
    Neorv32MipiTxState *s = NEORV32_MIPI_TX(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &neorv32_mipi_tx_ops, s,
                          TYPE_NEORV32_MIPI_TX, NEORV32_MIPI_TX_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
}

static void neorv32_mipi_tx_realize(DeviceState *dev, Error **errp)
{
    Neorv32MipiTxState *s = NEORV32_MIPI_TX(dev);

    timer_init_ns(&s->frame_timer, QEMU_CLOCK_VIRTUAL,
                  neorv32_mipi_tx_frame_timer, s);

    if (s->dumpfile) {
        s->dump = fopen(s->dumpfile, "wb");
        if (!s->dump) {
            error_setg_errno(errp, errno, "could not open MIPI TX dump file %s",
                             s->dumpfile);
            return;
        }
    }
}

static void neorv32_mipi_tx_unrealize(DeviceState *dev)
{
    Neorv32MipiTxState *s = NEORV32_MIPI_TX(dev);

    timer_del(&s->frame_timer);
    if (s->dump) {
        fclose(s->dump);
        s->dump = NULL;
    }
}

static const VMStateDescription vmstate_neorv32_mipi_tx = {
    .name = TYPE_NEORV32_MIPI_TX,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_TIMER(frame_timer, Neorv32MipiTxState),
        VMSTATE_UINT32(control, Neorv32MipiTxState),
        VMSTATE_UINT32(type_vc, Neorv32MipiTxState),
        VMSTATE_UINT16(pixels_per_line, Neorv32MipiTxState),
        VMSTATE_UINT16(n_lines, Neorv32MipiTxState),
        VMSTATE_UINT16(frame_end_word, Neorv32MipiTxState),
        VMSTATE_UINT16(tlp_sot_delay_clk, Neorv32MipiTxState),
        VMSTATE_UINT16(tlpx_delay_clk, Neorv32MipiTxState),
        VMSTATE_UINT16(tlp_sot_delay_data, Neorv32MipiTxState),
        VMSTATE_UINT16(tlpx_delay_data, Neorv32MipiTxState),
        VMSTATE_UINT16(tlp_sot_short, Neorv32MipiTxState),
        VMSTATE_UINT16(ths_prepare, Neorv32MipiTxState),
        VMSTATE_UINT16(ths_zero, Neorv32MipiTxState),
        VMSTATE_UINT16(ths_exit, Neorv32MipiTxState),
        VMSTATE_UINT8(n_mipi_lanes, Neorv32MipiTxState),
        VMSTATE_BOOL(hs_active, Neorv32MipiTxState),
        VMSTATE_BOOL(hs_data_valid, Neorv32MipiTxState),
        VMSTATE_BOOL(frame_done, Neorv32MipiTxState),
        VMSTATE_BOOL(irq_pending, Neorv32MipiTxState),
        VMSTATE_UINT16(frame_number, Neorv32MipiTxState),
        VMSTATE_UINT32(overlay_offset, Neorv32MipiTxState),
        VMSTATE_END_OF_LIST()
    },
};

static const Property neorv32_mipi_tx_properties[] = {
    DEFINE_PROP_STRING("dumpfile", Neorv32MipiTxState, dumpfile),
};

static void neorv32_mipi_tx_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = neorv32_mipi_tx_realize;
    dc->unrealize = neorv32_mipi_tx_unrealize;
    dc->vmsd = &vmstate_neorv32_mipi_tx;
    device_class_set_legacy_reset(dc, neorv32_mipi_tx_reset);
    device_class_set_props(dc, neorv32_mipi_tx_properties);
}

static const TypeInfo neorv32_mipi_tx_type_info = {
    .name = TYPE_NEORV32_MIPI_TX,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Neorv32MipiTxState),
    .instance_init = neorv32_mipi_tx_init,
    .class_init = neorv32_mipi_tx_class_init,
};

static void neorv32_mipi_tx_register_types(void)
{
    type_register_static(&neorv32_mipi_tx_type_info);
}

type_init(neorv32_mipi_tx_register_types)

Neorv32MipiTxState *neorv32_mipi_tx_create(MemoryRegion *address_space,
                                           hwaddr base)
{
    DeviceState *dev;
    SysBusDevice *sbd;

    dev = qdev_new(TYPE_NEORV32_MIPI_TX);
    qdev_set_id(dev, g_strdup("mipi-tx"), &error_fatal);
    sbd = SYS_BUS_DEVICE(dev);

    if (!sysbus_realize_and_unref(sbd, &error_fatal)) {
        return NULL;
    }

    memory_region_add_subregion(address_space, base,
                                sysbus_mmio_get_region(sbd, 0));
    return NEORV32_MIPI_TX(dev);
}
