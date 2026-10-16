/*
 * I2C target clock stretching interface (see include/hw/i2c/i2c_stretch.h)
 *
 * Copyright (c) 2026 VideoGPU
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/i2c/i2c.h"
#include "hw/i2c/i2c_stretch.h"

bool i2c_bus_stretched(I2CBus *bus, I2CStretchPoint point)
{
    I2CNode *node;

    QLIST_FOREACH(node, &bus->current_devs, next) {
        Object *obj = OBJECT(node->elt);

        if (object_dynamic_cast(obj, TYPE_I2C_TARGET_STRETCH)) {
            I2CTargetStretchClass *k = I2C_TARGET_STRETCH_GET_CLASS(obj);

            if (k->stretch && k->stretch(node->elt, point)) {
                return true;
            }
        }
    }
    return false;
}

bool i2c_bus_nacked(I2CBus *bus)
{
    I2CNode *node;

    QLIST_FOREACH(node, &bus->current_devs, next) {
        Object *obj = OBJECT(node->elt);

        if (object_dynamic_cast(obj, TYPE_I2C_TARGET_STRETCH)) {
            I2CTargetStretchClass *k = I2C_TARGET_STRETCH_GET_CLASS(obj);

            if (k->nacked && k->nacked(node->elt)) {
                return true;
            }
        }
    }
    return false;
}

static const TypeInfo i2c_target_stretch_info = {
    .name       = TYPE_I2C_TARGET_STRETCH,
    .parent     = TYPE_INTERFACE,
    .class_size = sizeof(I2CTargetStretchClass),
};

static void i2c_target_stretch_register_types(void)
{
    type_register_static(&i2c_target_stretch_info);
}

type_init(i2c_target_stretch_register_types)
