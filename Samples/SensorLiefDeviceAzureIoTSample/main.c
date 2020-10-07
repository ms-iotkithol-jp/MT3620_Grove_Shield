#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <pthread.h>
#include <errno.h>

#include <applibs/log.h>
#include <applibs/gpio.h>
#include <applibs/i2c.h>
#include <applibs/networking.h>
#include <applibs/eventloop.h>

#include "../../MT3620_Grove_Shield_Library/Grove.h"
#include "../../MT3620_Grove_Shield_Library/Sensors/GroveTempHumiBaroBME280.h"
#include "../../MT3620_Grove_Shield_Library/Sensors/GroveTempHumiSHT31.h"
#include "../../MT3620_Grove_Shield_Library/Sensors/GroveLEDButton.h"
#include "../../MT3620_Grove_Shield_Library/Sensors/GroveRelay.h"

#include "HardwareDefinitions/seeed_mt3620_mdb/inc/hw/template_appliance.h"

// MT3620 RDB: LED 1 (red channel)
#define SAMPLE_LED (8)  // MT3620_RDB_LED1_RED https://github.com/Azure/azure-sphere-samples/blob/master/Hardware/mt3620_rdb/inc/hw/sample_hardware.h

// Azure IoT SDK

#include <iothub_client_core_common.h>
#include <iothub_device_client_ll.h>
#include <iothub_client_options.h>
#include <iothubtransportmqtt.h>
#include <iothub.h>
#include <azure_sphere_provisioning.h>

#include "eventloop_timer_utilities.h"

#include "parson.h" // used to parse Device Twin messages.

#include "liefdevice.h"
#include "../../MT3620_Grove_Shield_Library/mt3620_rdb.h"

// This C application for the MT3620 Reference Development Board (Azure Sphere)
// outputs a string every second to Visual Studio's Device Output window
//
// It uses the API for the following Azure Sphere application libraries:
// - log (messages shown in Visual Studio's Device Output window during debugging)

static volatile sig_atomic_t terminationRequested = false;

/// <summary>
///     Signal handler for termination requests. This handler must be async-signal-safe.
/// </summary>
static void TerminationHandler(int signalNumber)
{
    // Don't use Log_Debug here, as it is not guaranteed to be async signal safe
    terminationRequested = true;
}



// Azure IoT defines.
static char* scopeId = NULL; // ScopeId for DPS
static IOTHUB_DEVICE_CLIENT_LL_HANDLE iothubClientHandle = NULL;
static const int keepalivePeriodSeconds = 20;
static bool iothubAuthenticated = false;


static EventLoop* eventLoop = NULL;
static EventLoopTimer* azureTimer = NULL;
static void AzureTimerEventHandler(EventLoopTimer* timer);

static void AzureTimerAlarmOff(EventLoopTimer* timer);

// Azure IoT poll periods
static const int AzureIoTDefaultPollPeriodSeconds = 1;        // poll azure iot every second
static const int AzureIoTPollPeriodsPerTelemetry = 5;         // only send telemetry 1/5 of polls
static const int AzureIoTMinReconnectPeriodSeconds = 60;      // back off when reconnecting
static const int AzureIoTMaxReconnectPeriodSeconds = 10 * 60; // back off limit
static int azureIoTPollPeriodSeconds = -1;

static bool WorkOnEventLoop();

// State variables

static GPIO_Value_Type sendMessageButtonState = GPIO_Value_High;
static bool statusLedOn = false;
// LED
static int deviceTwinStatusLedGpioFd = -1;

static int systemStatusNetworkLedGpioFd = -1;
static int systemStatusIoTHubLedGpioFd = -1;
static int systemStatusIoTStatusLedGpioFd = -1;
static int systemStatusIoTSending = -1;
static int systemStatusIoTRetry = -1;

static int blueLEDButtonButtonPinId = 1;
static int blueLEDButtonLEDPinId = 0;
static void* blueLEDButton = NULL;
static void* buzer = NULL;

// Azure IoT poll periods
/*
static const int AzureIoTDefaultPollPeriodSeconds = 1;        // poll azure iot every second
static const int AzureIoTPollPeriodsPerTelemetry = 5;         // only send telemetry 1/5 of polls
static const int AzureIoTMinReconnectPeriodSeconds = 60;      // back off when reconnecting
static const int AzureIoTMaxReconnectPeriodSeconds = 10 * 60; // back off limit

static int azureIoTPollPeriodSeconds = -1;
*/
// Function declarations
static void SendEventCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void* context);
static void SetupAzureClient(void);
static void DeviceTwinCallback(DEVICE_TWIN_UPDATE_STATE updateState, const unsigned char* payload,
    size_t payloadSize, void* userContextCallback);
static int DeviceMethodCallback(const char* methodName, const unsigned char* payload,
    size_t payloadSize, unsigned char** response, size_t* responseSize,
    void* userContextCallback);
static void ConnectionStatusCallback(IOTHUB_CLIENT_CONNECTION_STATUS result, IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason, void* userContextCallback); static void TwinReportState(const char* jsonState);
static void ReportedStateCallback(int result, void* context);
static bool CheckNetworkStatus();
static void MotorDriveOrder(const char* cmd);

static char timestamp[32];
static char messageBuf[256];

typedef struct SensorValue
{
    uint32_t availableFlag;
    float temperature;
    float humidity;
    float pressure;
} SensorValuePacket;

#define SENSOR_VALUE_ENABLE_MASK_TEMPERATURE (0x01)
#define SENSOR_VALUE_ENABLE_MASK_HUMIDITY (0x02)
#define SENSOR_VALUE_ENABLE_MASK_PRESSURE (0x04)

typedef SensorValuePacket* (*ReadSensorValueDelegate)();
static ReadSensorValueDelegate setupSensor();
static char* serializeMessage(SensorValuePacket* packet);

static int waitForSending = 1;

static int uartFd = -1;

/// <summary>
///     Main entry point for this sample.
/// </summary>
int main(int argc, char *argv[])
{
    Log_Debug("Application starting\n");

    systemStatusNetworkLedGpioFd = GPIO_OpenAsOutput(MT3620_RDB_LED1_BLUE, GPIO_OutputMode_PushPull, GPIO_Value_High);
    systemStatusIoTHubLedGpioFd = GPIO_OpenAsOutput(MT3620_RDB_LED2_RED, GPIO_OutputMode_PushPull, GPIO_Value_High);
    systemStatusIoTStatusLedGpioFd = GPIO_OpenAsOutput(MT3620_RDB_LED2_BLUE, GPIO_OutputMode_PushPull, GPIO_Value_High);
    GPIO_SetValue(systemStatusNetworkLedGpioFd, GPIO_Value_Low);
    GPIO_SetValue(systemStatusIoTHubLedGpioFd, GPIO_Value_Low);
    GPIO_SetValue(systemStatusIoTStatusLedGpioFd, GPIO_Value_Low);
    systemStatusIoTSending = GPIO_OpenAsOutput(MT3620_RDB_LED4_GREEN,GPIO_OutputMode_PushPull, GPIO_Value_High);
    systemStatusIoTRetry = GPIO_OpenAsOutput(MT3620_RDB_LED4_RED, GPIO_OutputMode_PushPull, GPIO_Value_High);

    uartFd = LiefDevice_Open(MT3620_ISU3_UART);
    if (uartFd < 0) {
        Log_Debug("Failed to open Uart comm.");
    }
    
    bool isNetworkingReady = CheckNetworkStatus();

    if (argc > 1) {
        scopeId = argv[1];
        Log_Debug("Using Azure IoT DPS Scope ID %s\n", scopeId);
    }
    else {
        Log_Debug("ScopeId needs to be set in the app_manifest CmdArgs for IoT Hub telemetry\n");
        isNetworkingReady = false;
    }

    if (isNetworkingReady)
    {
        SetupAzureClient();
    }

    // Register a SIGTERM handler for termination requests
    struct sigaction action;
    memset(&action, 0, sizeof(struct sigaction));
    action.sa_handler = TerminationHandler;
    sigaction(SIGTERM, &action, NULL);

    ReadSensorValueDelegate readSensorValueDelegate = setupSensor();

    // SAMPLE_LED is used to show Device Twin settings state
    Log_Debug("Opening SAMPLE_LED as output.\n");
    deviceTwinStatusLedGpioFd =
        GPIO_OpenAsOutput(SAMPLE_LED, GPIO_OutputMode_PushPull, GPIO_Value_High);
    if (deviceTwinStatusLedGpioFd == -1) {
        Log_Debug("ERROR: Could not open SAMPLE_LED: %s (%d).\n", strerror(errno), errno);
    }

    blueLEDButton = GroveLEDButton_Init(blueLEDButtonButtonPinId, blueLEDButtonLEDPinId);

    int loopIndex = 0;
    // Main loop
    while (!terminationRequested) {

        SensorValuePacket* packet = readSensorValueDelegate();

        if (iothubAuthenticated)
        {
            char* msgBuf = serializeMessage(packet);
            IOTHUB_MESSAGE_HANDLE messageHandle = IoTHubMessage_CreateFromString(msgBuf);
            if (messageHandle == 0) {
                Log_Debug("Error: unable to create a new IoTHubMessage.\n");
            }
            else {
                if (((loopIndex++) % waitForSending) == 0) {
                    if (IoTHubDeviceClient_LL_SendEventAsync(iothubClientHandle, messageHandle, SendEventCallback, NULL) != IOTHUB_CLIENT_OK)
                    {
                        Log_Debug("ERROR: failure requesting IoTHubClient to send telemetry event.\n");
                    }
                    else {
                        Log_Debug("INFO: IoTHubClient accepted the telemetry event for delivery.\n");
                    }
                }
            }
            GPIO_Value_Type sendingStatusLED;
            int ledValue = GPIO_GetValue(systemStatusIoTSending, &sendingStatusLED);
            if (ledValue==0) {
                if (sendingStatusLED == GPIO_Value_High) {
                    GPIO_SetValue(systemStatusIoTSending, GPIO_Value_Low);
                }
                else {
                    GPIO_SetValue(systemStatusIoTSending, GPIO_Value_High);
                }
            }
        }
        else {
            GPIO_SetValue(systemStatusIoTRetry, GPIO_Value_Low);
            if (CheckNetworkStatus()) {
                SetupAzureClient();
            }
            GPIO_SetValue(systemStatusIoTRetry, GPIO_Value_High);
        }

		usleep(1000000);
        WorkOnEventLoop();
    }

    Log_Debug("Application exiting\n");
    return 0;
}

static bool CheckNetworkStatus()
{
    bool isNetworkingReady = false;
    while (isNetworkingReady == false) {
        if ((Networking_IsNetworkingReady(&isNetworkingReady) == -1) || !isNetworkingReady) {
            Log_Debug("WARNING: Network is not ready. Device cannot connect until network is ready.\n");
        }
        if (isNetworkingReady) {
            GPIO_SetValue(systemStatusNetworkLedGpioFd, GPIO_Value_High);
            break;
        }
        usleep(1000000);
    }
    
    return isNetworkingReady;
}
/// <summary>
///     Converts AZURE_SPHERE_PROV_RETURN_VALUE to a string.
/// </summary>
static const char* GetAzureSphereProvisioningResultString(
    AZURE_SPHERE_PROV_RETURN_VALUE provisioningResult)
{
    switch (provisioningResult.result) {
    case AZURE_SPHERE_PROV_RESULT_OK:
        return "AZURE_SPHERE_PROV_RESULT_OK";
    case AZURE_SPHERE_PROV_RESULT_INVALID_PARAM:
        return "AZURE_SPHERE_PROV_RESULT_INVALID_PARAM";
    case AZURE_SPHERE_PROV_RESULT_NETWORK_NOT_READY:
        return "AZURE_SPHERE_PROV_RESULT_NETWORK_NOT_READY";
    case AZURE_SPHERE_PROV_RESULT_DEVICEAUTH_NOT_READY:
        return "AZURE_SPHERE_PROV_RESULT_DEVICEAUTH_NOT_READY";
    case AZURE_SPHERE_PROV_RESULT_PROV_DEVICE_ERROR:
        return "AZURE_SPHERE_PROV_RESULT_PROV_DEVICE_ERROR";
    case AZURE_SPHERE_PROV_RESULT_GENERIC_ERROR:
        return "AZURE_SPHERE_PROV_RESULT_GENERIC_ERROR";
    default:
        return "UNKNOWN_RETURN_VALUE";
    }
}
/// <summary>
///     Sets up the Azure IoT Hub connection (creates the iothubClientHandle)
///     When the SAS Token for a device expires the connection needs to be recreated
///     which is why this is not simply a one time call.
/// </summary>
static void SetupAzureClient(void)
{
    if (iothubClientHandle != NULL) {
        IoTHubDeviceClient_LL_Destroy(iothubClientHandle);
    }
    AZURE_SPHERE_PROV_RETURN_VALUE provResult =
        IoTHubDeviceClient_LL_CreateWithAzureSphereDeviceAuthProvisioning(scopeId, 10000,
            &iothubClientHandle);
    GPIO_SetValue(systemStatusIoTHubLedGpioFd, GPIO_Value_High);

    Log_Debug("IoTHubDeviceClient_LL_CreateWithAzureSphereDeviceAuthProvisioning returned '%s'.\n",
        GetAzureSphereProvisioningResultString(provResult));

    if (provResult.result != AZURE_SPHERE_PROV_RESULT_OK) {
        Log_Debug("ERROR: Failed to create IoTHub Handle\n");
        return;
    }
    GPIO_SetValue(systemStatusIoTStatusLedGpioFd, GPIO_Value_High);


    // Successfully connected, so make sure the polling frequency is back to the default
    iothubAuthenticated = true;

    if (IoTHubDeviceClient_LL_SetOption(iothubClientHandle, OPTION_KEEP_ALIVE,
        &keepalivePeriodSeconds) != IOTHUB_CLIENT_OK) {
        Log_Debug("ERROR: Failure setting Azure IoT Hub client option \"%s\".\n",
            OPTION_KEEP_ALIVE);
        return;
    }
    IoTHubDeviceClient_LL_SetDeviceTwinCallback(iothubClientHandle, DeviceTwinCallback, NULL);
    IoTHubDeviceClient_LL_SetDeviceMethodCallback(iothubClientHandle, DeviceMethodCallback, NULL);
    IoTHubDeviceClient_LL_SetConnectionStatusCallback(iothubClientHandle, ConnectionStatusCallback, NULL);

    if (eventLoop != NULL) {
        EventLoop_Stop(eventLoop);
        EventLoop_Close(eventLoop);
    }
    eventLoop = EventLoop_Create();
    if (eventLoop == NULL) {
        Log_Debug("ERROR: Could not create event loop.\n");
    }
    else {
        azureIoTPollPeriodSeconds = AzureIoTDefaultPollPeriodSeconds;
        struct timespec azureTelemetryPeriod = { .tv_sec = azureIoTPollPeriodSeconds, .tv_nsec = 0 };
        azureTimer =
            CreateEventLoopPeriodicTimer(eventLoop, &AzureTimerEventHandler, &azureTelemetryPeriod);
        if (azureTimer == NULL) {
            Log_Debug("ERROR: Failure creating azure timer!");
        }
    }
}

void ConnectionStatusCallback(IOTHUB_CLIENT_CONNECTION_STATUS result, IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason, void* userContextCallback)
{
    if (reason != IOTHUB_CLIENT_CONNECTION_OK) {
        Log_Debug("IoT Hub Disconnected");
        GPIO_SetValue(systemStatusIoTHubLedGpioFd, GPIO_Value_Low);
    }
    if (result == IOTHUB_CLIENT_CONNECTION_UNAUTHENTICATED) {
        iothubAuthenticated = false;
        GPIO_SetValue(systemStatusIoTStatusLedGpioFd, GPIO_Value_Low);
    }
}
/// <summary>
///     Callback invoked when the Azure IoT Hub send event request is processed.
/// </summary>
static void SendEventCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void* context)
{
    Log_Debug("INFO: Azure IoT Hub send telemetry event callback: status code %d.\n", result);
}

static void* sensorInstance;
static SensorValuePacket sensorValuePacket;

static int i2cFd;
void* bme280 = NULL;
void* sht31 = NULL;
static SensorValuePacket* readBME280()
{
    SensorValuePacket* result = &sensorValuePacket;
    if (bme280 == NULL)
    {
        Log_Debug("ERROR: BME280 is not built-in.\n");
        return NULL;
    }
    GroveTempHumiBaroBME280_Read(bme280);
    sensorValuePacket.temperature = GroveTempHumiBaroBME280_GetTemperature(bme280);
    sensorValuePacket.humidity = GroveTempHumiBaroBME280_GetHumidity(bme280);
    sensorValuePacket.pressure = GroveTempHumiBaroBME280_GetPressure(bme280);
    Log_Debug("Temperature: %.1f C\n", sensorValuePacket.temperature);
    Log_Debug("Humidity: %.1f\%c\n", sensorValuePacket.humidity, 0x25);
    Log_Debug("Pressure: %.1fP\n", sensorValuePacket.pressure);

    float discom = (float)0.81 * sensorValuePacket.temperature + (float)0.01 * sensorValuePacket.humidity * ((float)0.99 * sensorValuePacket.temperature - (float)14.3) + (float)46.3;
    Log_Debug("Discomfort Index: %.1f\n", discom);
    return result;
}

static SensorValuePacket* readSHT31()
{
    SensorValuePacket* result = &sensorValuePacket;
    if (sht31 == NULL)
    {
        Log_Debug("ERROR: SHT31 is not built-in.\n");
        return NULL;
    }
    GroveTempHumiSHT31_Read(sht31);
    sensorValuePacket.temperature = GroveTempHumiSHT31_GetTemperature(sht31);
    sensorValuePacket.humidity = GroveTempHumiSHT31_GetHumidity(sht31);
    Log_Debug("Temperature: %.1f C\n", sensorValuePacket.temperature);
    Log_Debug("Humidity: %.1f\%c\n", sensorValuePacket.humidity, 0x25);

    float discom = (float)0.81 * sensorValuePacket.temperature + (float)0.01 * sensorValuePacket.humidity * ((float)0.99 * sensorValuePacket.temperature - (float)14.3) + (float)46.3;
    Log_Debug("Discomfort Index: %.1f\n", discom);

    return result;
}

static SensorValuePacket* readSimulator()
{
    // generate a simulated temperature
    static float temperature = 50.0f;                    // starting temperature
    float delta = ((float)(rand() % 41)) / 20.0f - 1.0f; // between -1.0 and +1.0
    temperature += delta;

    sensorValuePacket.temperature = temperature;
    return &sensorValuePacket;
}

static bool existGroveShield = false;
static bool existBME280 = false;
static bool existSHT31 = false;

static void* threadCheckGroveShield(void* arg) {
    uint32_t baudrate = (uint32_t)arg;
    Log_Debug("GroveShield initializing with %d.\n", baudrate);
    int fd;
    GroveShield_Initialize(&fd, baudrate);
    existGroveShield = true;

    Log_Debug("GroveShield initialization completed fd=%d\n.", fd);
    pthread_exit((void*)fd);
}


/// <summary>
/// Check sensor connection and initialize that.
/// </summary>
static ReadSensorValueDelegate setupSensor()
{
    ReadSensorValueDelegate delegateMethod = NULL;
    sensorValuePacket.availableFlag = SENSOR_VALUE_ENABLE_MASK_TEMPERATURE;
    int i2cFd;
    int baudrate = 115200;
    long checkTimeoutUSec = 3000000;

    pthread_t checkThreadGS;
    if (pthread_create(&checkThreadGS, NULL, threadCheckGroveShield, (void*)baudrate) != 0) {
        perror("pthread_create() error");
        exit(1);
    }
    usleep(checkTimeoutUSec);
    if (!existGroveShield) {
        pthread_cancel(checkThreadGS);
        Log_Debug("INFO: Grove Shield is not built-in.\n");
    }
    else {
        void* ret;
        if (pthread_join(checkThreadGS, &ret) != 0) {
            perror("pthread_create() error");
            exit(3);
        }
        else {
            i2cFd = (int)ret;
        }
        sht31 = GroveTempHumiSHT31_Open(i2cFd);
        if (GroveI2C_CheckMaxTryCountForChecktatus()) {
            Log_Debug("INFO: Confirmed connecting SHT31\n");
            sensorValuePacket.availableFlag |= SENSOR_VALUE_ENABLE_MASK_HUMIDITY;
            delegateMethod = readSHT31;
        }
        else {
            if ((bme280 = GroveTempHumiBaroBME280_Open(i2cFd))!=0) {
                Log_Debug("INFO: Confirmed connecting BME280\n");
                sensorValuePacket.availableFlag |= (SENSOR_VALUE_ENABLE_MASK_PRESSURE | SENSOR_VALUE_ENABLE_MASK_HUMIDITY);
                delegateMethod = readBME280;
            }
        }
    }
    if (delegateMethod == NULL) {
        Log_Debug("INFO: Any sensors are not built-in. Select Simulator.\n");
        delegateMethod = readSimulator;
    }

    return delegateMethod;
}

static char* serializeMessage(SensorValuePacket* packet)
{
    char vBuf[32];
    time_t now;
    now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    sprintf(timestamp, "%04d/%02d/%02dT%02d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    sprintf(messageBuf, "\"timestamp\":\"%s\"", timestamp);
    if ((packet->availableFlag & SENSOR_VALUE_ENABLE_MASK_TEMPERATURE) != 0) {
        sprintf(vBuf, ",\"temperature\":%.1f", packet->temperature);
        strcat(messageBuf, vBuf);
    }
    if ((packet->availableFlag & SENSOR_VALUE_ENABLE_MASK_HUMIDITY) != 0) {
        sprintf(vBuf, ",\"humidity\":%.1f", packet->humidity);
        strcat(messageBuf, vBuf);
    }
    if ((packet->availableFlag & SENSOR_VALUE_ENABLE_MASK_PRESSURE) != 0) {
        sprintf(vBuf, ",\"pressure\":%.1f", packet->pressure);
        strcat(messageBuf, vBuf);
    }
    strcat(messageBuf, "}");
    return messageBuf;
}

EventLoopTimer* alarmOffTimer = NULL;
/// <summary>
///     Callback invoked when a Direct Method is received from Azure IoT Hub.
/// </summary>
static int DeviceMethodCallback(const char* methodName, const unsigned char* payload,
    size_t payloadSize, unsigned char** response, size_t* responseSize,
    void* userContextCallback)
{
    int result;
    char* responseString;
    unsigned char payloadJson[payloadSize+1];
    strncpy(payloadJson, payload, payloadSize);

    Log_Debug("Received Device Method callback: Method name %s.\n", methodName);
    if (payloadSize > 0) {
        payloadJson[payloadSize] = '\0';
    }
    if (strcmp("MotorDrive", methodName) == 0) {
        responseString = "\"Invalid MotorDrive Order\"";
        Log_Debug("MotorDrive Invoked\n");
        JSON_Value* root_value = json_parse_string(payloadJson);
        JSON_Value_Type jsonValueType = json_value_get_type(root_value);
        if (jsonValueType == JSONString) {
            const char* motorCommandJson = json_value_get_string(root_value);
            JSON_Value* contentValue = json_parse_string(motorCommandJson);
            jsonValueType = json_value_get_type(contentValue);
            if (jsonValueType == JSONObject) {
                JSON_Object* contentObject = json_value_get_object(contentValue);
                const char* motorCommand = json_object_get_string(contentObject, "command");
                Log_Debug("Command:%s\n", motorCommand);
                MotorDriveOrder(motorCommand);
                responseString = "\"Received MotorDrive invocation - string style\"";
            }
        }
        else if (jsonValueType == JSONObject) {
            JSON_Object* contentObject = json_value_get_object(root_value);
            const char* motorCommand = json_object_get_string(contentObject, "command");
            Log_Debug("Command:%s\n", motorCommand);
            MotorDriveOrder(motorCommand);
            responseString = "\"Received MotorDrive invocation - json style\"";
        }
        else {
        }
        result = 200;
    }
    else if (strcmp("TriggerAlarm", methodName) == 0) {
        // Output alarm using Log_Debug
        Log_Debug("  ----- ALARM TRIGGERED! -----\n");
        GroveLEDButton_LedOff(blueLEDButton);

        responseString = "\"Alarm Triggered\""; // must be a JSON string (in quotes)
        GroveLEDButton_LedOn(blueLEDButton);
        result = 200;
        if (alarmOffTimer == NULL) {
            alarmOffTimer = CreateEventLoopDisarmedTimer(eventLoop, AzureTimerAlarmOff);
            if (alarmOffTimer == NULL) {
                Log_Debug("Trgger Alarm Timer creation is failed!");
            }
            else {
                struct timespec alarmOffDuration = { .tv_sec = 5, .tv_nsec = 0 };
                SetEventLoopTimerOneShot(alarmOffTimer, &alarmOffDuration);
                Log_Debug("TriggerAlarm timer created");
            }
        }
    }
    else if (strcmp("StopAlarm", methodName) == 0) {
        Log_Debug("  ----- ALARM STOP! -----\n");
        responseString = "\"Alarm Stopped\""; // must be a JSON string (in quotes)
        GroveLEDButton_LedOff(blueLEDButton);
        result = 200;
    }
    else {
        // All other method names are ignored
        responseString = "{}";
        result = -1;
    }

    // if 'response' is non-NULL, the Azure IoT library frees it after use, so copy it to heap
    *responseSize = strlen(responseString);
    *response = malloc(*responseSize);
    memcpy(*response, responseString, *responseSize);
    return result;
}


/// <summary>
///     Callback invoked when a Device Twin update is received from Azure IoT Hub.
/// </summary>
static void DeviceTwinCallback(DEVICE_TWIN_UPDATE_STATE updateState, const unsigned char* payload,
    size_t payloadSize, void* userContextCallback)
{
    size_t nullTerminatedJsonSize = payloadSize + 1;
    char* nullTerminatedJsonString = (char*)malloc(nullTerminatedJsonSize);
    if (nullTerminatedJsonString == NULL) {
        Log_Debug("ERROR: Could not allocate buffer for twin update payload.\n");
        abort();
    }

    // Copy the provided buffer to a null terminated buffer.
    memcpy(nullTerminatedJsonString, payload, payloadSize);
    // Add the null terminator at the end.
    nullTerminatedJsonString[nullTerminatedJsonSize - 1] = 0;

    JSON_Value* rootProperties = NULL;
    rootProperties = json_parse_string(nullTerminatedJsonString);
    if (rootProperties == NULL) {
        Log_Debug("WARNING: Cannot parse the string as JSON content.\n");
        goto cleanup;
    }

    JSON_Object* rootObject = json_value_get_object(rootProperties);
    JSON_Object* desiredProperties = json_object_dotget_object(rootObject, "desired");
    if (desiredProperties == NULL) {
        desiredProperties = rootObject;
    }

    // The desired properties should have a "StatusLED" object
    JSON_Object* LEDState = json_object_dotget_object(desiredProperties, "StatusLED");
    if (LEDState != NULL) {
        // ... with a "value" field which is a Boolean
        int statusLedValue = json_object_get_boolean(LEDState, "value");
        if (statusLedValue != -1) {
            statusLedOn = statusLedValue == 1;
            GPIO_SetValue(deviceTwinStatusLedGpioFd,
                statusLedOn ? GPIO_Value_Low : GPIO_Value_High);
            if (blueLEDButton != NULL) {
                if (statusLedOn) {
                    GroveLEDButton_LedOn(blueLEDButton);
                }
                else {
                    GroveLEDButton_LedOff(blueLEDButton);
                }
            }
        }
    }

    int reqWaitForSending = (int)json_object_dotget_number(desiredProperties, "TimingForSending");
    if (reqWaitForSending > 0) {
        waitForSending = reqWaitForSending;
        Log_Debug("Updated waitForSending by %d\n", waitForSending);
    }

    // Report current status LED state
    if (statusLedOn) {
        TwinReportState("{\"StatusLED\":{\"value\":true}}");
    }
    else {
        TwinReportState("{\"StatusLED\":{\"value\":false}}");
    }

cleanup:
    // Release the allocated memory.
    json_value_free(rootProperties);
    free(nullTerminatedJsonString);
}

static void TwinReportState(const char* jsonState)
{
    if (iothubClientHandle == NULL) {
        Log_Debug("ERROR: Azure IoT Hub client not initialized.\n");
    }
    else {
        if (IoTHubDeviceClient_LL_SendReportedState(
            iothubClientHandle, (const unsigned char*)jsonState, strlen(jsonState),
            ReportedStateCallback, NULL) != IOTHUB_CLIENT_OK) {
            Log_Debug("ERROR: Azure IoT Hub client error when reporting state '%s'.\n", jsonState);
        }
        else {
            Log_Debug("INFO: Azure IoT Hub client accepted request to report state '%s'.\n",
                jsonState);
        }
    }
}

/// <summary>
///     Callback invoked when the Device Twin report state request is processed by Azure IoT Hub
///     client.
/// </summary>
static void ReportedStateCallback(int result, void* context)
{
    Log_Debug("INFO: Azure IoT Hub Device Twin reported state callback: status code %d.\n", result);
}

/// <summary>
/// Azure timer event:  Check connection status and send telemetry
/// </summary>
static void AzureTimerEventHandler(EventLoopTimer* timer)
{
    if (ConsumeEventLoopTimerEvent(timer) != 0) {
        Log_Debug("ERROR: failure notify event consuming");
        return;
    }
    bool isNetworkingReady = false;
    if ((Networking_IsNetworkingReady(&isNetworkingReady) == -1) || !isNetworkingReady) {
        Log_Debug("WARNING: Network is not ready. Device cannot connect until network is ready.\n");
    }
    if (scopeId==NULL) {
        Log_Debug("ScopeId needs to be set in the app_manifest CmdArgs for IoT Hub telemetry\n");
        isNetworkingReady = false;
    }

    if (isNetworkingReady && iothubAuthenticated==false)
    {
        SetupAzureClient();
    }

    if (iothubAuthenticated) {
        Log_Debug("INFO: iot hub work doing...");
        IoTHubDeviceClient_LL_DoWork(iothubClientHandle);
    }
}

static void AzureTimerAlarmOff(EventLoopTimer* timer)
{
    if (ConsumeEventLoopTimerEvent(timer) != 0) {
        Log_Debug("ERROR: failure notify event consuming");
        return;
    }
    GroveLEDButton_LedOff(blueLEDButton);

    alarmOffTimer = NULL;
}

bool WorkOnEventLoop()
{
    EventLoop_Run_Result result = EventLoop_Run(eventLoop, -1, true);
    if (result == EventLoop_Run_Failed) {
        return false;
    }
    return true;
}

void MotorDriveOrder(const char* cmd)
{
    LiefDevice_SendText(uartFd, cmd);
/*
    char* cmds[2] = { cmd,0 };
    char* cmd2 = strchr(cmd, (int)';');
    if (cmd2 != 0) {
        cmd2[0] = '\0';
        cmd2 = cmd2 + 1;
        cmds[1] = cmd2;
    }
    for (int i = 0; i < 2; i++) {
        if (cmds[i]!=0)
            LiefDevice_SendText(uartFd, cmds[i]);
    }
    */
}