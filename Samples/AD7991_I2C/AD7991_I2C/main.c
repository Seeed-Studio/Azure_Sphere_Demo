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
#include "epoll_timerfd_utilities.h"

#include "AD7992.h"

#include "mt3620_rdb.h"

// This C application for the MT3620 Reference Development Board (Azure Sphere)
// outputs a string every second to Visual Studio's Device Output window
//
// It uses the API for the following Azure Sphere application libraries:
// - log (messages shown in Visual Studio's Device Output window during debugging)

int i2cFd = -1;
void *AD7992Fd = NULL;
static volatile sig_atomic_t terminationRequired = false;

/// <summary>
///     Signal handler for termination requests. This handler must be async-signal-safe.
/// </summary>
static void TerminationHandler(int signalNumber)
{
	// Don't use Log_Debug here, as it is not guaranteed to be async-signal-safe.
	terminationRequired = true;
}

static int InitPeripheralsAndHandlers(void)
{
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

	int result = I2CMaster_SetBusSpeed(i2cFd, I2C_BUS_SPEED_STANDARD);
	if (result != 0) {
		Log_Debug("ERROR: I2CMaster_SetBusSpeed: errno=%d (%s)\n", errno, strerror(errno));
		return -1;
	}

	result = I2CMaster_SetTimeout(i2cFd, 100);
	if (result != 0) {
		Log_Debug("ERROR: I2CMaster_SetTimeout: errno=%d (%s)\n", errno, strerror(errno));
		return -1;
	}

	result = I2CMaster_SetDefaultTargetAddress(i2cFd, AD7992_I2CAddress);
	if (result != 0) {
		Log_Debug("ERROR: I2CMaster_SetDefaultTargetAddress: errno=%d (%s)\n", errno,
			strerror(errno));
		return -1;
	}

	AD7992Fd = GroveAD7992_Open(i2cFd, MT3620_GPIO41, MT3620_GPIO42);
	//AD7992Fd = GroveAD7992_Open(i2cFd, MT3620_GPIO58, MT3620_GPIO57);

	return 0;
}

/// <summary>
///     Close peripherals and handlers.
/// </summary>
static void ClosePeripheralsAndHandlers(void)
{	
	Log_Debug("Closing file descriptors.\n");
	CloseFdAndPrintError(i2cFd, "i2c");
}
/// <summary>
///     Main entry point for this sample.
/// </summary>
int main(int argc, char *argv[])
{
	Log_Debug("Application starting.\n");
	

	InitPeripheralsAndHandlers();

	// Main loop
	static const struct timespec loopInterval = { 0, 100000000 };
	while (!terminationRequired) {
		uint16_t A0 = GroveAD7992_Read(AD7992Fd, 0);
		uint16_t A1 = GroveAD7992_Read(AD7992Fd, 1);
		Log_Debug("A0: %d A1: %d\n", A0, A1);
		nanosleep(&loopInterval, NULL);
	}

	////////////////////////////////////////
	// Terminate
	ClosePeripheralsAndHandlers();
	Log_Debug("Application exiting\n");
	return 0;
}
