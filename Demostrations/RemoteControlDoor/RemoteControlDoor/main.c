#include <errno.h>
#include <unistd.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

#include "applibs_versions.h"
#include "epoll_timerfd_utilities.h"
#include <applibs/log.h>
#include <applibs/uart.h>
#include <applibs/gpio.h>

#include "mt3620_rdb.h"

#include "led_blink_utility.h"
#include "timer_utility.h"
#include <applibs/wificonfig.h>

#include "azure_iot_utilities.h"

#define  DOOR_STATE_OPEN  GPIO_Value_Low
#define  DOOR_STATE_CLOSE  GPIO_Value_High

// Change ssid and psk for your WiFi router 
const char *wifiSsid = "SSID";
const char *wifiPsk = "PSK";

//int uart3Fd = -1;
//static const struct timespec LoopInterval = { 0, 200000000 };
static volatile sig_atomic_t terminationRequired = false;

// GPIO or other device fd
static int blinkingLedGpioFd = -1;
static int doorControlPinFd = -1;
static int doorStatePinFd = -1;
static int cautionLedFd = -1;
static bool door_state = NAN;

// State 
static bool cautionStatus = -1;
static GPIO_Value_Type ledState;
static GPIO_Value_Type doorState;

// Timer Fd
static int epollFd = -1;
static int blinkingLedTimerFd = -1;
static int azureIoTDoWorkTimerFd = -1;
static int doorCheckStateTimerFd = -1;
static int cautionLedControlTimerFd = -1;
static int fingerprintScanTimerFd = -1;
static int fingerprintRecordBTNCheckingTimerFd = -1;

// Azure IoT poll periods
static const int AzureIoTDefaultPollPeriodSeconds = 5;
static const int AzureIoTMinReconnectPeriodSeconds = 60;
static const int AzureIoTMaxReconnectPeriodSeconds = 10 * 60;

static int azureIoTPollPeriodSeconds = -1;

// UART parameters
static int uartFd = -1;
static size_t totalBytesReceived = 0;

// Blink interval 
static const struct timespec blinkIntervals[] = { {0, 125000000}, {0, 250000000}, {0, 500000000}, {1, 0} };

// State judgement
static int isStopFingerprintScan = false;
static int isDoorOpenedByFingerprint = false;

// Connectivity state
static bool connectedToIoTHub = false;

static void AzureIoTHubSendAlarm(void);

static void TerminationHandler(int signalNumber)
{
	// Don't use Log_Debug here, as it is not guaranteed to be async signal safe
	terminationRequired = true;
}

///// <summary>
/////     Helper function to send a fixed message via the given UART.
///// </summary>
///// <param name="uartFd">The open file descriptor of the UART to write to</param>
///// <param name="dataToSend">The data to send over the UART</param>
//static void SendUartMessage(int uartFd, const char *dataToSend)
//{
//	size_t totalBytesSent = 0;
//	size_t totalBytesToSend = strlen(dataToSend);
//	int sendIterations = 0;
//	while (totalBytesSent < totalBytesToSend) {
//		sendIterations++;
//
//		// Send as much of the remaining data as possible
//		size_t bytesLeftToSend = totalBytesToSend - totalBytesSent;
//		const char *remainingMessageToSend = dataToSend + totalBytesSent;
//		ssize_t bytesSent = write(uartFd, remainingMessageToSend, bytesLeftToSend);
//		if (bytesSent < 0) {
//			Log_Debug("ERROR: Could not write to UART: %s (%d).\n", strerror(errno), errno);
//			terminationRequired = true;
//			return;
//		}
//
//		totalBytesSent += (size_t)bytesSent;
//	}
//
//	Log_Debug("Sent %zu bytes over UART in %d calls.\n", totalBytesSent, sendIterations);
//}


/// <summary>
///     Handle UART event: if there is incoming data, print it, and blink the LED.
/// </summary>
static void UartEventHandler(EventData *eventData)
{

	//TO-DO: Stop all UART send timer event 


	//uint8_t scanOkResponse[] = { 0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0x00, 0x08, 0x00, 0x05, 0x00, 0x01, 0x00, 0x64, 0x00, 0x79 };
	uint8_t scanOkResponse[] = { 0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0x0, 0x07, 0x00, 0x08, 0x00, 0x05, 0x00, 0x01, 0x00, 0x64, 0x00, 0x79 };
	const size_t receiveBufferSize = 256;
	uint8_t receiveBuffer[receiveBufferSize + 1]; // allow extra byte for string termination
	ssize_t bytesRead;

	// Read UART message
	//while (read(uartFd, receiveBuffer, receiveBufferSize) > 0)
	//{
	//	totalBytesReceived += receiveBufferSize;
	//}
	bytesRead = read(uartFd, &receiveBuffer[totalBytesReceived], receiveBufferSize);
	if (bytesRead < 0) {
		Log_Debug("ERROR: Could not read UART: %s (%d).\n", strerror(errno), errno);
		terminationRequired = true;
		return;
	}

	if (bytesRead > 0) {
		// if (receiveBuffer[0] != 0xEF)
		// {
        //     ;
		// }
		// Null terminate the buffer to make it a valid string, and print it
		receiveBuffer[bytesRead] = 0;				
		// If the total received bytes is odd, turn the LED on, otherwise turn it off
		totalBytesReceived += (size_t)bytesRead;
	}

	// Check if scan response data is if correct
	if(totalBytesReceived >= sizeof(scanOkResponse))
	{
		uint8_t correct = 0;
		Log_Debug("UART read: ");
		for (uint8_t i = 0; i < totalBytesReceived; i++)
		{
			Log_Debug("0x%X ", receiveBuffer[i]);
			if (receiveBuffer[i] == scanOkResponse[i])
			{
				correct++;
			}
			else
			{
				correct = 0;
			}
		}
		Log_Debug("\r\n");

		if (correct == sizeof(scanOkResponse))
		{
			// Open the door
			GPIO_SetValue(doorControlPinFd, GPIO_Value_Low);
			Log_Debug("Fingerprint recognized, door opened.\n"); 
            isDoorOpenedByFingerprint = true;
            AzureIoTHubSendAlarm();
			correct = 0;
		}

		totalBytesReceived = 0;
	}
}

static void finderprint_record_start(void)
{
	uint8_t recCmd[] = { 0xEF, 0x01, 0xff, 0xff, 0xFF, 0xFF, 0x01, 0x00, 0x08, 0x31, 0x00, 0x01, 0x04, 0x00, 0x08, 0x00, 0x47 };

	Log_Debug("Start fingerprint record: ");
	for (uint8_t i = 0; i < sizeof(recCmd); i++)
	{
		write(uartFd, &recCmd[i], 1);
		Log_Debug("%X ", recCmd[i]);
	}
	Log_Debug("\n");
}
static void finderprint_scan_start(void)
{
	uint8_t recCmd[] = { 0xEF, 0x01, 0xff, 0xff, 0xFF, 0xFF, 0x01, 0x00, 0x08, 0x32, 0x01, 0x00, 0x01, 0x00, 0x04, 0x00, 0x41 };
	if(!isStopFingerprintScan)
    {
        Log_Debug("Start fingerprint scan: ");
        for (uint8_t i = 0; i < sizeof(recCmd); i++)
        {
            write(uartFd, &recCmd[i], 1);
            Log_Debug("%X ", recCmd[i]);
        }
        Log_Debug("\n");
    }    
}

//static void UARTHandler()
//{
//	const size_t receiveBufferSize = 64;
//	uint8_t receiveBuffer[receiveBufferSize + 1]; // allow extra byte for string termination
//	ssize_t bytesRead;
//
//	// Read UART message
//	bytesRead = read(uart3Fd, receiveBuffer, receiveBufferSize);
//	if (bytesRead > 0) {
//		Log_Debug(receiveBuffer);
//		// Need Maintain
//		if (NULL != (strstr(receiveBuffer, "Need Maintain")))
//		{
//			AzureIoT_SendMessage("{\"body\":{\"message\":\"Fan Need Maintain\"}}");
//			Log_Debug("Fan Need Maintain.\n");
//			// Set the send/receive LED to blink once immediately to indicate the message has been
//			// queued
//			LedBlinkUtility_BlinkNow(&ledMessageEventSentReceived, LedBlinkUtility_Colors_Red);
//		}
//	}
//}


/// <summary>
///     Handle LED timer event: blink LED.
/// </summary>
static void BlinkingLedTimerEventHandler(EventData *eventData)
{
	if (ConsumeTimerFdEvent(blinkingLedTimerFd) != 0) {
		terminationRequired = true;
		return;
	}

	// The blink interval has elapsed, so toggle the LED state
	// The LED is active-low so GPIO_Value_Low is on and GPIO_Value_High is off
	ledState = (ledState == GPIO_Value_Low ? GPIO_Value_High : GPIO_Value_Low);
	int result = GPIO_SetValue(blinkingLedGpioFd, ledState);
	if (result != 0) {
		Log_Debug("ERROR: Could not set LED output value: %s (%d).\n", strerror(errno), errno);
		terminationRequired = true;
	}
}

/// <summary>
///     Handle door state checking timer event: checking door state.
/// </summary>
static void DoorStateCheckingTimerEventHandler(EventData *eventData)
{
	if (ConsumeTimerFdEvent(doorCheckStateTimerFd) != 0) {
		terminationRequired = true;
		return;
	}

	// CLose the door every 2 seconds
	GPIO_SetValue(doorControlPinFd, GPIO_Value_High);

    GPIO_GetValue(doorStatePinFd, &doorState);
	Log_Debug("Info: Periodicly checking door state: %d\n", doorState);
}

/// <summary>
///     Handle control caution led timer event: control caution led.
/// </summary>
static void CautionLedControlTimerEventHandler(EventData *eventData)
{
	if (ConsumeTimerFdEvent(cautionLedControlTimerFd) != 0) {
		terminationRequired = true;
		return;
	}

    // TO-DO: Alarm condition setting
    //if(doorState == GPIO_Value_Low && doorControlStatus == true) // Door state is High and doorControlState is high then caution trigger
    if(doorState == DOOR_STATE_OPEN)
    {
		cautionStatus = GPIO_Value_High;
        GPIO_SetValue(cautionLedFd, GPIO_Value_High);
        AzureIoTHubSendAlarm();
    }
    else
    {
        cautionStatus = GPIO_Value_Low;
        GPIO_SetValue(cautionLedFd, GPIO_Value_Low);
    }
    
}

static void AzureIoTHubSendAlarm(void)
{
    // Send IoT message to mention the caution
        if (connectedToIoTHub) {            
			// Send a message
			AzureIoT_SendMessage("Door is opened!");            
			// Turn caution led on            
			GPIO_SetValue(cautionLedFd, GPIO_Value_High);
        } else {
            Log_Debug("WARNING: Cannot send message: not connected to the IoT Hub.\n");
        }
}
/// <summary>
///     Hand over control periodically to the Azure IoT SDK's DoWork.
/// </summary>
static void AzureIoTDoWorkHandler(EventData *eventData)
{
    if (ConsumeTimerFdEvent(azureIoTDoWorkTimerFd) != 0) {
        terminationRequired = true;
        return;
    }

    // Set up the connection to the IoT Hub client.
    // Notes it is safe to call this function even if the client has already been set up, as in
    //   this case it would have no effect
    if (AzureIoT_SetupClient()) {
        if (azureIoTPollPeriodSeconds != AzureIoTDefaultPollPeriodSeconds) {
            azureIoTPollPeriodSeconds = AzureIoTDefaultPollPeriodSeconds;

            struct timespec azureTelemetryPeriod = {azureIoTPollPeriodSeconds, 0};
            SetTimerFdToPeriod(azureIoTDoWorkTimerFd, &azureTelemetryPeriod);
        }

        // AzureIoT_DoPeriodicTasks() needs to be called frequently in order to keep active
        // the flow of data with the Azure IoT Hub
        AzureIoT_DoPeriodicTasks();
    } else {
        // If we fail to connect, reduce the polling frequency, starting at
        // AzureIoTMinReconnectPeriodSeconds and with a backoff up to
        // AzureIoTMaxReconnectPeriodSeconds
        if (azureIoTPollPeriodSeconds == AzureIoTDefaultPollPeriodSeconds) {
            azureIoTPollPeriodSeconds = AzureIoTMinReconnectPeriodSeconds;
        } else {
            azureIoTPollPeriodSeconds *= 2;
            if (azureIoTPollPeriodSeconds > AzureIoTMaxReconnectPeriodSeconds) {
                azureIoTPollPeriodSeconds = AzureIoTMaxReconnectPeriodSeconds;
            }
        }

        struct timespec azureTelemetryPeriod = {azureIoTPollPeriodSeconds, 0};
        SetTimerFdToPeriod(azureIoTDoWorkTimerFd, &azureTelemetryPeriod);

        Log_Debug("ERROR: Failed to connect to IoT Hub; will retry in %i seconds\n",
                  azureIoTPollPeriodSeconds);
    }
}

static void FingerprintScanEventHandler(EventData *eventData)
{
	if (ConsumeTimerFdEvent(fingerprintScanTimerFd) != 0) {
		terminationRequired = true;
		return;
	}

	if(doorState == DOOR_STATE_CLOSE)
	    finderprint_scan_start();
}

static void FingerprintRecordBTNCheckingEventHandler(EventData *eventData)
{
    if (ConsumeTimerFdEvent(fingerprintRecordBTNCheckingTimerFd) != 0) {
		terminationRequired = true;
		return;
	}

    if(doorState == DOOR_STATE_OPEN )
    {

    }	    
}

// event handler data structures. Only the event handler field needs to be populated
static EventData blinkingLedTimerEventData = { .eventHandler = &BlinkingLedTimerEventHandler };
static EventData doorStateCheckingTimerEventData = { .eventHandler = &DoorStateCheckingTimerEventHandler };
static EventData azureIoTEventData = {.eventHandler = &AzureIoTDoWorkHandler};
static EventData cautionLedControlTimerEventData = {.eventHandler = &CautionLedControlTimerEventHandler};
static EventData uartEventData = { .eventHandler = &UartEventHandler };
static EventData fingerprintScanTimerEventData = { .eventHandler = &FingerprintScanEventHandler };
static EventData fingerprintRecordBTNCheckingTimerEventData = { .eventHandler = &FingerprintRecordBTNCheckingEventHandler };

static void DeviceTwinUpdate(JSON_Object *desiredProperties)
{
	if (SetTimerFdToPeriod(blinkingLedTimerFd, &blinkIntervals[3]) != 0)
	{
		terminationRequired = true;
	}

	JSON_Value *lockDoorJson = json_object_get_value(desiredProperties, "lock_door");
	if (lockDoorJson != NULL && json_value_get_type(lockDoorJson) == JSONBoolean)
	{
		bool lockCommand = json_value_get_boolean(lockDoorJson);
		Log_Debug("lock_door = %d\n", lockCommand);
		GPIO_SetValue(doorControlPinFd, lockCommand ? GPIO_Value_High : GPIO_Value_Low);

		JSON_Value *doorLockedStateJson = json_object_get_value(desiredProperties, "door_locked_state");
		if (doorLockedStateJson != NULL && json_value_get_type(doorLockedStateJson) == JSONBoolean)
		{
			bool state = json_value_get_boolean(doorLockedStateJson);
			Log_Debug("Transmit door_locked_state = %d\n", state);
			AzureIoT_TwinReportState("door_locked_state", (size_t)doorLockedStateJson);	// TODO Cannot report floating-point value.
			door_state = state;
		}
	}
}
/// <summary>
///     Allocates and formats a string message on the heap.
/// </summary>
/// <param name="messageFormat">The format of the message</param>
/// <param name="maxLength">The maximum length of the formatted message string</param>
/// <returns>The pointer to the heap allocated memory.</returns>
static void *SetupHeapMessage(const char *messageFormat, size_t maxLength, ...)
{
	va_list args;
	va_start(args, maxLength);
	char *message =
		malloc(maxLength + 1); // Ensure there is space for the null terminator put by vsnprintf.
	if (message != NULL) {
		vsnprintf(message, maxLength, messageFormat, args);
	}
	va_end(args);
	return message;
}

/// <summary>
///     Direct Method callback function, called when a Direct Method call is received from the Azure
///     IoT Hub.
/// </summary>
/// <param name="methodName">The name of the method being called.</param>
/// <param name="payload">The payload of the method.</param>
/// <param name="responsePayload">The response payload content. This must be a heap-allocated
/// string, 'free' will be called on this buffer by the Azure IoT Hub SDK.</param>
/// <param name="responsePayloadSize">The size of the response payload content.</param>
/// <returns>200 HTTP status code if the method name is "LedColorControlMethod" and the color is
/// correctly parsed;
/// 400 HTTP status code is the color has not been recognised in the payload;
/// 404 HTTP status code if the method name is unknown.</returns>
static int DirectMethodCall(const char *methodName, const char *payload, size_t payloadSize,
	char **responsePayload, size_t *responsePayloadSize)
{
	// Prepare the payload for the response. This is a heap allocated null terminated string.
	// The Azure IoT Hub SDK is responsible of freeing it.
	*responsePayload = NULL;  // Reponse payload content.
	*responsePayloadSize = 0; // Response payload content size.

	int result = 404; // HTTP status code.

	if (strcmp(methodName, "fingerprint_record") == 0) {		
		// The payload should contains JSON such as: {"record_ID": 1, "record_time" : 4}
		//char *directMethodCallContent = malloc(payloadSize + 1); // +1 to store null char at the end.
		//if (directMethodCallContent == NULL) {
		//	Log_Debug("ERROR: Could not allocate buffer for direct method request payload.\n");
		//	abort();
		//}
		//memcpy(directMethodCallContent, payload, payloadSize);

		//directMethodCallContent[payloadSize] = 0; // Null terminated string.
		//JSON_Value *payloadJson = json_parse_string(directMethodCallContent);
		//if (payloadJson == NULL) {
		//	goto payloadNotFound;
		//}
		//JSON_Object *commandJson = json_value_get_object(payloadJson);
		//if (commandJson == NULL) {
		//	goto payloadNotFound;
		//}
		//const char *value = json_object_get_string(commandJson, "record");
		//if (value == NULL) {
		//	goto payloadNotFound;
		//}


		// Stop finger scan				
		isStopFingerprintScan = true;

		// Start record fingerprint
		finderprint_record_start();

		//Start finger print
		isStopFingerprintScan = false;
	}
	else if(strcmp(methodName, "door_open") == 0)
	{
		Log_Debug("INFO: Method called: '%s'.\n", methodName);
		GPIO_SetValue(doorControlPinFd, GPIO_Value_Low);
	}
	else if (strcmp(methodName, "door_close") == 0)
	{
		Log_Debug("INFO: Method called: '%s'.\n", methodName);
		GPIO_SetValue(doorControlPinFd, GPIO_Value_High);
	}
	else
	{
		result = 404;
		Log_Debug("INFO: Method not found called: '%s'.\n", methodName);

		static const char noMethodFound[] = "\"method not found '%s'\"";
		size_t responseMaxLength = sizeof(noMethodFound) + strlen(methodName);
		*responsePayload = SetupHeapMessage(noMethodFound, responseMaxLength, methodName);
		if (*responsePayload == NULL) {
			Log_Debug("ERROR: Could not allocate buffer for direct method response payload.\n");
			abort();
		}
		*responsePayloadSize = strlen(*responsePayload);
		return result;
	}

	// Color's name has been identified.
	result = 200;

	static const char OkResponse[] =
		"{ \"success\" : true, \"method\" : \"%s\" }";
	size_t responseMaxLength = sizeof(OkResponse) + strlen(payload);
	*responsePayload = SetupHeapMessage(OkResponse, responseMaxLength, methodName);
	if (*responsePayload == NULL) {
		Log_Debug("ERROR: Could not allocate buffer for direct method response payload.\n");
		abort();
	}
	*responsePayloadSize = strlen(*responsePayload);
	

	return result;

payloadNotFound:
	result = 400; // Bad request.
	Log_Debug("INFO: Unrecognised direct method payload format.\n");

	static const char noColorResponse[] =
		"{ \"success\" : false, \"message\" : \"request does not contain an identifiable "
		"record\" }";
	responseMaxLength = sizeof(noColorResponse);
	*responsePayload = SetupHeapMessage(noColorResponse, responseMaxLength);
	if (*responsePayload == NULL) {
		Log_Debug("ERROR: Could not allocate buffer for direct method response payload.\n");
		abort();
	}
	*responsePayloadSize = strlen(*responsePayload);

	return result;
}
/// <summary>
///     IoT Hub connection status callback function.
/// </summary>
/// <param name="connected">'true' when the connection to the IoT Hub is established.</param>
static void IoTHubConnectionStatusChanged(bool connected)
{
	connectedToIoTHub = connected;
	if (SetTimerFdToPeriod(blinkingLedTimerFd, &blinkIntervals[3]) != 0)
	{
		terminationRequired = true;
	}
}


/// <summary>
///     Store a sample WiFi network.
/// </summary>
//static void AddSampleWiFiNetwork(void)
//{
//	int result = 0;
//	result = WifiConfig_StoreWpa2Network((uint8_t*)wifiSsid, strlen(wifiSsid),
//		wifiPsk, strlen(wifiPsk));
//	if (result < 0) {
//		if (errno == EEXIST) {
//			Log_Debug("INFO: The \"%s\" WiFi network is already stored on the device.\n", wifiSsid);
//		}
//		else {
//			Log_Debug(
//				"ERROR: WifiConfig_StoreOpenNetwork failed to store WiFi network \"%s\" with "
//				"result %d. Errno: %s (%d).\n",
//				wifiSsid, result, strerror(errno), errno);
//		}
//	}
//	else {
//		Log_Debug("INFO: Successfully stored WiFi network: \"%s\".\n", wifiSsid);
//	}
//}

/// <summary>
///     List all stored WiFi networks.
/// </summary>
static void DebugPrintStoredWiFiNetworks(void)
{
	int result = WifiConfig_GetStoredNetworkCount();
	if (result < 0) {
		Log_Debug(
			"ERROR: WifiConfig_GetStoredNetworkCount failed to get stored network count with "
			"result %d. Errno: %s (%d).\n",
			result, strerror(errno), errno);
	}
	else if (result == 0) {
		Log_Debug("INFO: There are no stored WiFi networks.\n");
	}
	else {
		Log_Debug("INFO: There are %d stored WiFi networks:\n", result);
		size_t networkCount = (size_t)result;
		WifiConfig_StoredNetwork *networks =
			(WifiConfig_StoredNetwork *)malloc(sizeof(WifiConfig_StoredNetwork) * networkCount);
		int result = WifiConfig_GetStoredNetworks(networks, networkCount);
		if (result < 0) {
			Log_Debug(
				"ERROR: WifiConfig_GetStoredNetworks failed to get stored networks with "
				"result %d. Errno: %s (%d).\n",
				result, strerror(errno), errno);
		}
		else {
			networkCount = (size_t)result;
			for (size_t i = 0; i < networkCount; ++i) {
				Log_Debug("INFO: %3d) SSID \"%.*s\"\n", i, networks[i].ssidLength,
					networks[i].ssid);
			}
		}
		free(networks);
	}
}

/// <summary>
///     Show details of the currently connected WiFi network.
/// </summary>
static void DebugPrintCurrentlyConnectedWiFiNetwork(void)
{
	WifiConfig_ConnectedNetwork network;
	int result = WifiConfig_GetCurrentNetwork(&network);
	if (result < 0) {
		Log_Debug("INFO: Not currently connected to a WiFi network.\n");
	}
	else {
		Log_Debug("INFO: Currently connected WiFi network: ");
		Log_Debug("INFO: SSID \"%.*s\", BSSID %02x:%02x:%02x:%02x:%02x:%02x, Frequency %dMHz\n",
			network.ssidLength, network.ssid, network.bssid[0], network.bssid[1],
			network.bssid[2], network.bssid[3], network.bssid[4], network.bssid[5],
			network.frequencyMHz);
	}
}

/// <summary>
///     Trigger a WiFi scan and list found WiFi networks.
/// </summary>
// static void DebugPrintScanFoundNetworks(void)
// {
//     int result = WifiConfig_TriggerScanAndGetScannedNetworkCount();
//     if (result < 0) {
//         Log_Debug(
//             "ERROR: WifiConfig_TriggerScanAndGetScannedNetworkCount failed to get scanned "
//             "network count with result %d. Errno: %s (%d).\n",
//             result, strerror(errno), errno);
//     } else if (result == 0) {
//         Log_Debug("INFO: Scan found no WiFi network.\n");
//     } else {
//         size_t networkCount = (size_t)result;
//         Log_Debug("INFO: Scan found %d WiFi networks:\n", result);
//         WifiConfig_ScannedNetwork *networks =
//             (WifiConfig_ScannedNetwork *)malloc(sizeof(WifiConfig_ScannedNetwork) * networkCount);
//         result = WifiConfig_GetScannedNetworks(networks, networkCount);
//         if (result < 0) {
//             Log_Debug(
//                 "ERROR: WifiConfig_GetScannedNetworks failed to get scanned networks with "
//                 "result %d. Errno: %s (%d).\n",
//                 result, strerror(errno), errno);
//         } else {
//             // Log SSID, signal strength and frequency of the found WiFi networks
//             networkCount = (size_t)result;
//             for (size_t i = 0; i < networkCount; ++i) {
//                 Log_Debug("INFO: %3d) SSID \"%.*s\", Signal Level %d, Frequency %dMHz\n", i,
//                           networks[i].ssidLength, networks[i].ssid, networks[i].signalRssi,
//                           networks[i].frequencyMHz);
//             }
//         }
//         free(networks);
//     }
// }

static int InitPeripheralsAndHandlers(void)
{
	struct sigaction action;
	memset(&action, 0, sizeof(struct sigaction));
	action.sa_handler = TerminationHandler;
	sigaction(SIGTERM, &action, NULL);

	epollFd = CreateEpollFd();
	if (epollFd < 0) {
		return -1;
	}

	// Create a UART_Config object, open the UART and set up UART event handler
	UART_Config uartConfig;
	UART_InitConfig(&uartConfig);
	uartConfig.baudRate = 57600;
	uartConfig.flowControl = UART_FlowControl_None;
	uartFd = UART_Open(MT3620_RDB_HEADER2_ISU0_UART, &uartConfig);
	if (uartFd < 0) {
		Log_Debug("ERROR: Could not open UART: %s (%d).\n", strerror(errno), errno);
		return -1;
	}
	if (RegisterEventHandlerToEpoll(epollFd, uartFd, &uartEventData, EPOLLIN) != 0) {
		return -1;
	}

	// Init blinking LED
	blinkingLedGpioFd = GPIO_OpenAsOutput(MT3620_GPIO7, GPIO_OutputMode_PushPull, GPIO_Value_High);
	if (blinkingLedGpioFd < 0) {
		Log_Debug("ERROR: Could not open LED GPIO: %s (%d).\n", strerror(errno), errno);
		return -1;
	}

	// doorControlPinFd output
	doorControlPinFd = GPIO_OpenAsOutput(MT3620_GPIO6, GPIO_OutputMode_PushPull, GPIO_Value_High);
	if (doorControlPinFd < 0) {
		Log_Debug("ERROR: Could not open GPIO: %s (%d).\n", strerror(errno), errno);
		return -1;
	}

	// cautionLedFd output
	cautionLedFd = GPIO_OpenAsOutput(MT3620_GPIO30, GPIO_OutputMode_PushPull, GPIO_Value_Low);
	if (cautionLedFd < 0) {
		Log_Debug("ERROR: Could not open GPIO: %s (%d).\n", strerror(errno), errno);
		return -1;
	}

	// doorStatePinFd input
	doorStatePinFd = GPIO_OpenAsInput(MT3620_GPIO35);
	if (doorStatePinFd < 0) {
		Log_Debug("ERROR: Could not open GPIO: %s (%d).\n", strerror(errno), errno);
		return -1;
	}	

	// Perform WiFi network setup and debug printing
	// AddSampleWiFiNetwork();
	DebugPrintStoredWiFiNetworks();
	DebugPrintCurrentlyConnectedWiFiNetwork();
	// DebugPrintScanFoundNetworks();

	////////////////////////////////////////
	// AzureIoT
	if (!AzureIoT_Initialize())
	{
		Log_Debug("ERROR: Cannot initialize Azure IoT Hub SDK.\n");
		return -1;
	}
    // Set the Azure IoT hub related callbacks
	AzureIoT_SetDeviceTwinUpdateCallback(&DeviceTwinUpdate);
	AzureIoT_SetDirectMethodCallback(&DirectMethodCall);
	AzureIoT_SetConnectionStatusCallback(&IoTHubConnectionStatusChanged);
	AzureIoT_SetupClient();
   
    // Set up a timer for led blinking
    blinkingLedTimerFd = CreateTimerFdAndAddToEpoll(epollFd, &blinkIntervals[0],
		&blinkingLedTimerEventData, EPOLLIN);
	if (blinkingLedTimerFd < 0) {
		return -1;
	}

    // Set up a timer for door state checking
    static const struct timespec doorStateCheckingInterval = {2, 0};
    doorCheckStateTimerFd = CreateTimerFdAndAddToEpoll(epollFd, &doorStateCheckingInterval, &doorStateCheckingTimerEventData, EPOLLIN);
	if (blinkingLedTimerFd < 0) {
		return -1;
	}

    // Set up a timer for caution led controling
    static const struct timespec cautionLedControlInterval = {2, 0};
    cautionLedControlTimerFd = CreateTimerFdAndAddToEpoll(epollFd, &cautionLedControlInterval, &cautionLedControlTimerEventData, EPOLLIN);
	if (cautionLedControlTimerFd < 0) {
		return -1;
	}

    // Set up a timer for Azure IoT SDK DoWork execution.
    azureIoTPollPeriodSeconds = AzureIoTDefaultPollPeriodSeconds;
    struct timespec azureIoTDoWorkPeriod = {azureIoTPollPeriodSeconds, 0};
    azureIoTDoWorkTimerFd = CreateTimerFdAndAddToEpoll(epollFd, &azureIoTDoWorkPeriod, &azureIoTEventData, EPOLLIN);
    if (azureIoTDoWorkTimerFd < 0) {
        return -1;
    }
	
	// Set up timer for finderprint scanning
	static const struct timespec fingerprintScanInterval = { 2, 0 };
	fingerprintScanTimerFd = CreateTimerFdAndAddToEpoll(epollFd, &fingerprintScanInterval, &fingerprintScanTimerEventData, EPOLLIN);
	if (cautionLedControlTimerFd < 0) {
		return -1;
	}

    // Set up timer for fingerprint record button checking
	static const struct timespec fingerprintRecordButtonCheckingInterval = { 2, 0 };
	fingerprintRecordBTNCheckingTimerFd = CreateTimerFdAndAddToEpoll(epollFd, &fingerprintRecordButtonCheckingInterval, &fingerprintRecordBTNCheckingTimerEventData, EPOLLIN);
	if (cautionLedControlTimerFd < 0) {
		return -1;
	}

	return 0;

}

/// <summary>
///     Close peripherals and handlers.
/// </summary>
static void ClosePeripheralsAndHandlers(void)
{
    Log_Debug("INFO: Closing GPIOs and Azure IoT client.\n");
	// Leave the LED off
	if (blinkingLedGpioFd >= 0) {
		GPIO_SetValue(blinkingLedGpioFd, GPIO_Value_High);
	}

	Log_Debug("Closing file descriptors.\n");
	CloseFdAndPrintError(blinkingLedTimerFd, "BlinkingLedTimer");
	CloseFdAndPrintError(blinkingLedGpioFd, "BlinkingLedGpio");
    CloseFdAndPrintError(azureIoTDoWorkTimerFd, "IoTDoWorkTimer");
	CloseFdAndPrintError(epollFd, "Epoll");

    // Destroy the IoT Hub client
    AzureIoT_DestroyClient();
    AzureIoT_Deinitialize();
}

int main(int argc, char *argv[])
{
	Log_Debug("AzureSphereDemo remote control door starting\n");

	InitPeripheralsAndHandlers();

	////////////////////////////////////////
	// Loop	
	while (!terminationRequired)
	{
		//UARTHandler();
		if (WaitForEventAndCallHandler(epollFd) != 0) {
			terminationRequired = true;
		}
	}

	////////////////////////////////////////
	// Terminate
	ClosePeripheralsAndHandlers();
	Log_Debug("Application exiting\n");
	return 0;
}
