/*
 * I2C target clock stretching
 *
 * QEMU's I2C bus is synchronous: a master calls i2c_send()/i2c_recv() and
 * the target answers at once. A target that, like real hardware, holds SCL
 * low until its firmware is ready (e.g. the NEORV32 i2c-target IP) implements
 * this interface. A master that supports stretching asks the targets of the
 * current transfer before every byte and waits (letting the guest run) while
 * any of them stretches.
 *
 * Copyright (c) 2026 VideoGPU
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_I2C_STRETCH_H
#define HW_I2C_STRETCH_H

#include "hw/i2c/i2c.h"
#include "qom/object.h"

#define TYPE_I2C_TARGET_STRETCH "i2c-target-stretch"

typedef struct I2CTargetStretchClass I2CTargetStretchClass;
DECLARE_CLASS_CHECKERS(I2CTargetStretchClass, I2C_TARGET_STRETCH,
                       TYPE_I2C_TARGET_STRETCH)

typedef enum I2CStretchPoint {
    I2C_STRETCH_BEFORE_SEND,    /* before the master sends a data byte */
    I2C_STRETCH_AFTER_SEND,     /* ACK slot of a byte the master sent */
    I2C_STRETCH_BEFORE_RECV,    /* before the target must drive a byte */
} I2CStretchPoint;

struct I2CTargetStretchClass {
    InterfaceClass parent;

    /* true while the target holds SCL low at @point */
    bool (*stretch)(I2CSlave *s, I2CStretchPoint point);
    /* after I2C_STRETCH_AFTER_SEND released: true if the byte was NACKed */
    bool (*nacked)(I2CSlave *s);
};

/* true if any target of the current transfer on @bus stretches at @point */
bool i2c_bus_stretched(I2CBus *bus, I2CStretchPoint point);

/* true if any target of the current transfer NACKed the last byte sent */
bool i2c_bus_nacked(I2CBus *bus);

#endif
