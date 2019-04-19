#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// applibs_versions.h defines the API struct versions to use for applibs APIs.
#include "applibs_versions.h"
#include <applibs/log.h>
#include <applibs/i2c.h>

#include "mt3620_rdb.h"


// I2C ADC registers
#define ADDR_ADC121             0x50
#define REG_ADDR_RESULT         0x00
#define REG_ADDR_ALERT          0x01
#define REG_ADDR_CONFIG         0x02
#define REG_ADDR_LIMITL         0x03
#define REG_ADDR_LIMITH         0x04
#define REG_ADDR_HYST           0x05
#define REG_ADDR_CONVL          0x06
#define REG_ADDR_CONVH          0x07

// This C application for the MT3620 Reference Development Board (Azure Sphere)
// outputs a string every second to Visual Studio's Device Output window
//
// It uses the API for the following Azure Sphere application libraries:
// - log (messages shown in Visual Studio's Device Output window during debugging)

static volatile sig_atomic_t terminationRequired = false;
static int i2cFd = -1;

static void InitPeripherals(void);

/// <summary>
///     Signal handler for termination requests. This handler must be async-signal-safe.
/// </summary>
static void TerminationHandler(int signalNumber)
{
	// Don't use Log_Debug here, as it is not guaranteed to be async-signal-safe.
	terminationRequired = true;
}

static void InitPeripherals(void)
{
	int result;
	uint8_t data[3] = {0};

	// Register a SIGTERM handler for termination requests
	struct sigaction action;
	memset(&action, 0, sizeof(struct sigaction));
	action.sa_handler = TerminationHandler;
	sigaction(SIGTERM, &action, NULL);

	// Initialize I2C master
	i2cFd = I2CMaster_Open(MT3620_RDB_HEADER4_ISU12_I2C);
	if (i2cFd < 0) {
		Log_Debug("ERROR: I2CMaster_Open: errno=%d (%s)\n", errno, strerror(errno));
		return -1;
	}

	result = I2CMaster_SetBusSpeed(i2cFd, I2C_BUS_SPEED_STANDARD);
	if (result != 0) {
		Log_Debug("ERROR: I2CMaster_SetBusSpeed: errno=%d (%s)\n", errno, strerror(errno));
		return -1;
	}

	result = I2CMaster_SetTimeout(i2cFd, 100);
	if (result != 0) {
		Log_Debug("ERROR: I2CMaster_SetTimeout: errno=%d (%s)\n", errno, strerror(errno));
		return -1;
	}

	result = I2CMaster_SetDefaultTargetAddress(i2cFd, ADDR_ADC121);
	if (result != 0) {
		Log_Debug("ERROR: I2CMaster_SetDefaultTargetAddress: errno=%d (%s)\n", errno,
			strerror(errno));
		return -1;
	}

	data[0] = REG_ADDR_CONFIG;
	data[1] = 0x20;

	// Turn on module power
	I2CMaster_Write(i2cFd, ADDR_ADC121, data, 2);
}

/// <summary>
///     Main entry point for this sample.
/// </summary>
int main(int argc, char *argv[])
 {
	uint8_t data[3] = {0};
	uint16_t adc_val = 0;

	Log_Debug("Application starting.\n");

	InitPeripherals();

	// Main loop
	const struct timespec sleepTime = { 0, 500000000 };
	while (!terminationRequired) {
		//Read data	
		data[0] = REG_ADDR_RESULT;

		ssize_t transferredBytes = I2CMaster_WriteThenRead(i2cFd, ADDR_ADC121, &data[0], 1, &data[1], 2);
		if (transferredBytes == 0) {
			Log_Debug("ERROR: I2CMaster_WriteThenRead: errno=%d (%s)\n", errno,
				strerror(errno));
			//return -1;
		}
		else
		{
			adc_val = (data[1] & 0x0F) << 8;
			adc_val |= data[2];
			Log_Debug("Read ADC: %dmV\r\n", 5000 * adc_val / 4095);
		}

		nanosleep(&sleepTime, NULL);
	}

	Log_Debug("Application exiting.\n");
	return 0;
}
