#include "AD7992.h"
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#include "applibs_versions.h"
#include <applibs/gpio.h>
#include <applibs/log.h>

typedef struct
{
	int I2cFd;
	int ConvstFd;
	int AlertFd;
}
AD7992Instance;

void* GroveAD7992_Open(int i2cFd, GPIO_Id convstId, GPIO_Id alertId)
{
	AD7992Instance* this = (AD7992Instance*)malloc(sizeof(AD7992Instance));

	this->I2cFd = i2cFd;
	this->ConvstFd = GPIO_OpenAsOutput(convstId, GPIO_OutputMode_PushPull, GPIO_Value_High);
	this->AlertFd = GPIO_OpenAsInput(alertId);

	return this;
}

uint16_t GroveAD7992_Read(void* inst, int channel)
{
	uint8_t data[2];
	uint16_t value;

	AD7992Instance* this = (AD7992Instance*)inst;	

	// Select channel
	data[0] = AD7992_REG_CONFIGURATION;
	data[1] = (channel == 0 ? 0x20 : 0x10) | 0x08;
	I2CMaster_Write(this->I2cFd, AD7992_I2CAddress, data, 2);

	// Start conversion
	GPIO_SetValue(this->ConvstFd, GPIO_Value_Low);

	// Wait for converted
	const struct timespec t_convert = { 0, 2000 };
	nanosleep(&t_convert, NULL);

	// Read value	
	data[0] = AD7992_REG_CONVERSION_RESULT;
	I2CMaster_WriteThenRead(this->I2cFd, AD7992_I2CAddress, data, 1, data, 2);

	// Stop conversion
	GPIO_SetValue(this->ConvstFd, GPIO_Value_High);

	value = (uint16_t)(data[1] | (data[0] & 0x0f) << 8);

	return value;
}
