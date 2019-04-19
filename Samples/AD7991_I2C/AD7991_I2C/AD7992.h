#pragma once

#include "applibs_versions.h"
#include <applibs/gpio.h>
#include <applibs/i2c.h>

#define AD7992_I2CAddress			    0x20

#define AD7992_REG_CONVERSION_RESULT	(0x0)
#define AD7992_REG_CONFIGURATION		(0x2)

void* GroveAD7992_Open(int i2cFd, GPIO_Id convstId, GPIO_Id alertId);
uint16_t GroveAD7992_Read(void* inst, int channel);
