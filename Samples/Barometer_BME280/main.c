#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <applibs/log.h>
#include <applibs/gpio.h>

#include "../../MT3620_Grove_Shield_Library/Grove.h"
#include "../../MT3620_Grove_Shield_Library/Sensors/GroveTempHumiBaroBME280.h"

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

/// <summary>
///     Main entry point for this sample.
/// </summary>
int main(int argc, char *argv[])
{
    Log_Debug("Application starting\n");

    // Register a SIGTERM handler for termination requests
    struct sigaction action;
    memset(&action, 0, sizeof(struct sigaction));
    action.sa_handler = TerminationHandler;
    sigaction(SIGTERM, &action, NULL);

	int i2cFd;
	GroveShield_Initialize(&i2cFd, 115200);
	void* bme280 = GroveTempHumiBaroBME280_Open(i2cFd);

    // Main loop
    while (!terminationRequested) {
        Log_Debug("Hello world\n");

		GroveTempHumiBaroBME280_Read(bme280);
		float temp = GroveTempHumiBaroBME280_GetTemperature(bme280);
		float humi = GroveTempHumiBaroBME280_GetHumidity(bme280);
        float pres = GroveTempHumiBaroBME280_GetPressure(bme280);

		Log_Debug("Temperature: %.1f C\n", temp);
		Log_Debug("Humidity: %.1f\%c\n", humi, 0x25);
        Log_Debug("Pressure: %.1fP\n", pres);

     //   float discom = (float)0.81 * temp + (float)0.01 * humi * ((float)0.99 * temp - (float)14.3) + (float)46.3;
      //  Log_Debug("Discomfort Index: %.1f\n", discom);

		usleep(1000000);
    }

    Log_Debug("Application exiting\n");
    return 0;
}
