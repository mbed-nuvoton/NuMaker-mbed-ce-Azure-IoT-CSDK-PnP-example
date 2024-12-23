// Copyright (c) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE file in the project root for full license information.

// This sample implements a PnP based Temperature Control model.  It demonstrates a complex 
// model in that the model has properties, commands, and telemetry off of the root component
// as well as subcomponents.

// The DTDL for component is https://github.com/Azure/iot-plugandplay-models/blob/main/dtmi/com/example/temperaturecontroller-1.json

// Standard C header files
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Mbed port header files
#include "mbed.h"
#include "NTPClient.h"

// IoTHub Device Client and IoT core utility related header files
#include "iothub.h"
#include "iothub_device_client_ll.h"
#include "iothub_client_options.h"
#include "iothub_message.h"
#include "azure_c_shared_utility/strings.h"
#include "azure_c_shared_utility/threadapi.h"
#include "azure_c_shared_utility/xlogging.h"

// PnP utilities.
extern "C" {
    #include "pnp_device_client_ll.h"
    #include "pnp_protocol.h"
}

// Headers that provide implementation for subcomponents (the two thermostat components and DeviceInfo)
extern "C" {
    #include "pnp_thermostat_component.h"
    #include "pnp_deviceinfo_component.h"
}

#include "aziot_user_config.h"

// Values of connection / security settings read from environment variables and/or DPS runtime
PNP_DEVICE_CONFIGURATION g_pnpDeviceConfiguration;

// Amount of time to sleep between polling hub, in milliseconds.  Set to wake up every 100 milliseconds.
static unsigned int g_sleepBetweenPollsMs = 100;

// Every time the main loop wakes up, on the g_sendTelemetryPollInterval(th) pass will send a telemetry message.
// So we will send telemetry every (g_sendTelemetryPollInterval * g_sleepBetweenPollsMs) milliseconds; 60 seconds as currently configured.
static const int g_sendTelemetryPollInterval = 600;

// Whether tracing at the IoTHub client is enabled or not. 
static bool g_hubClientTraceEnabled = MBED_CONF_APP_IOTHUB_CLIENT_TRACE;

// DTMI indicating this device's ModelId.
static const char g_temperatureControllerModelId[] = "dtmi:com:example:TemperatureController;1";

// PNP_THERMOSTAT_COMPONENT_HANDLE represent the thermostat components that are sub-components of the temperature controller.
// Note that we do NOT have an analogous DeviceInfo component handle because there is only DeviceInfo subcomponent and its
// implementation is straightforward.
PNP_THERMOSTAT_COMPONENT_HANDLE g_thermostatHandle1;
PNP_THERMOSTAT_COMPONENT_HANDLE g_thermostatHandle2;

// Name of subcomponents that TemmperatureController implements.
static const char g_thermostatComponent1Name[] = "thermostat1";
static const size_t g_thermostatComponent1Size = sizeof(g_thermostatComponent1Name) - 1;
static const char g_thermostatComponent2Name[] = "thermostat2";
static const size_t g_thermostatComponent2Size = sizeof(g_thermostatComponent2Name) - 1;
static const char g_deviceInfoComponentName[] = "deviceInformation";

static const char* g_modeledComponents[] = {g_thermostatComponent1Name, g_thermostatComponent2Name, g_deviceInfoComponentName};
static const size_t g_numModeledComponents = sizeof(g_modeledComponents) / sizeof(g_modeledComponents[0]);

// Command implemented by the TemperatureControl component itself to implement reboot.
static const char g_rebootCommand[] = "reboot";

// An empty JSON body for PnP command responses
static const char g_JSONEmpty[] = "{}";
static const size_t g_JSONEmptySize = sizeof(g_JSONEmpty) - 1;

// Minimum value we will return for working set, + some random number
static const int g_workingSetMinimum = 1000;
// Random number for working set will range between the g_workingSetMinimum and (g_workingSetMinimum+g_workingSetRandomModulo)
static const int g_workingSetRandomModulo = 500;
// Format string for sending a telemetry message with the working set.
static const char g_workingSetTelemetryFormat[] = "{\"workingSet\":%d}";

// Name of the serial number property as defined in this component's DTML
static const char g_serialNumberPropertyName[] = "serialNumber";
// Value of the serial number.  NOTE: This must be a legal JSON string which requires value to be in "..."
static const char g_serialNumberPropertyValue[] = "\"serial-no-123-abc\"";

//
// PnP_TempControlComponent_InvokeRebootCommand processes the reboot command on the root interface
//
static int PnP_TempControlComponent_InvokeRebootCommand(JSON_Value* rootValue)
{
    int result;

    if (json_value_get_type(rootValue) != JSONNumber)
    {
        LogError("Delay payload is not a number");
        result = PNP_STATUS_BAD_FORMAT;
    }
    else
    {
        // See caveats section in ../readme.md; we don't actually respect the delay value to keep the sample simple.
        int delayInSeconds = (int)json_value_get_number(rootValue);
        LogInfo("Temperature controller 'reboot' command invoked with delay=%d seconds", delayInSeconds);
        result = PNP_STATUS_SUCCESS;
    }
    
    return result;
}

//
// SetEmptyCommandResponse sets the response to be an empty JSON.  IoT Hub wants
// legal JSON, regardless of error status, so if command implementation did not set this do so here.
//
static void SetEmptyCommandResponse(unsigned char** response, size_t* responseSize, int* result)
{
    if ((*response = (unsigned char *)calloc(1, g_JSONEmptySize)) == NULL)
    {
        LogError("Unable to allocate empty JSON response");
        *result = PNP_STATUS_INTERNAL_ERROR;
    }
    else
    {
        memcpy(*response, g_JSONEmpty, g_JSONEmptySize);
        *responseSize = g_JSONEmptySize;
        // We only overwrite the caller's result on error; otherwise leave as it was
    }
}

//
// PnP_TempControlComponent_DeviceMethodCallback is invoked by IoT SDK when a device method arrives.
//
static int PnP_TempControlComponent_DeviceMethodCallback(const char* methodName, const unsigned char* payload, size_t size, unsigned char** response, size_t* responseSize, void* userContextCallback)
{
    (void)userContextCallback;

    char* jsonStr = NULL;
    JSON_Value* rootValue = NULL;
    int result;
    unsigned const char *componentName;
    size_t componentNameSize;
    const char *pnpCommandName;

    *response = NULL;
    *responseSize = 0;

    // Parse the methodName into its PnP (optional) componentName and pnpCommandName.
    PnP_ParseCommandName(methodName, &componentName, &componentNameSize, &pnpCommandName);

    // Parse the JSON of the payload request.
    if ((jsonStr = PnP_CopyPayloadToString(payload, size)) == NULL)
    {
        LogError("Unable to allocate twin buffer");
        result = PNP_STATUS_INTERNAL_ERROR;
    }
    else if ((rootValue = json_parse_string(jsonStr)) == NULL)
    {
        LogError("Unable to parse twin JSON");
        result = PNP_STATUS_INTERNAL_ERROR;
    }
    else
    {
        if (componentName != NULL)
        {
            LogInfo("Received PnP command for component=%.*s, command=%s", (int)componentNameSize, componentName, pnpCommandName);
            if (strncmp((const char*)componentName, g_thermostatComponent1Name, g_thermostatComponent1Size) == 0)
            {
                result = PnP_ThermostatComponent_ProcessCommand(g_thermostatHandle1, pnpCommandName, rootValue, response, responseSize);
            }
            else if (strncmp((const char*)componentName, g_thermostatComponent2Name, g_thermostatComponent2Size) == 0)
            {
                result = PnP_ThermostatComponent_ProcessCommand(g_thermostatHandle2, pnpCommandName, rootValue, response, responseSize);
            }
            else
            {
                LogError("PnP component=%.*s is not supported by TemperatureController", (int)componentNameSize, componentName);
                result = PNP_STATUS_NOT_FOUND;
            }
        }
        else
        {
            LogInfo("Received PnP command for TemperatureController component, command=%s", pnpCommandName);
            if (strcmp(pnpCommandName, g_rebootCommand) == 0)
            {
                result = PnP_TempControlComponent_InvokeRebootCommand(rootValue);
            }
            else
            {
                LogError("PnP command=s%s is not supported by TemperatureController", pnpCommandName);
                result = PNP_STATUS_NOT_FOUND;
            }
        }
    }

    if (*response == NULL)
    {
        SetEmptyCommandResponse(response, responseSize, &result);
    }

    json_value_free(rootValue);
    free(jsonStr);

    return result;
}

//
// PnP_TempControlComponent_ApplicationPropertyCallback is the callback function is invoked when PnP_ProcessTwinData() visits each property.
//
static void PnP_TempControlComponent_ApplicationPropertyCallback(const char* componentName, const char* propertyName, JSON_Value* propertyValue, int version, void* userContextCallback)
{
    // This sample uses the pnp_device_client.h/.c to create the IOTHUB_DEVICE_CLIENT_LL_HANDLE as well as initialize callbacks.
    // The convention used is that IOTHUB_DEVICE_CLIENT_LL_HANDLE is passed as the userContextCallback on the initial twin callback.
    // The pnp_protocol.h/.c pass this userContextCallback down to this visitor function.
    IOTHUB_DEVICE_CLIENT_LL_HANDLE deviceClient = (IOTHUB_DEVICE_CLIENT_LL_HANDLE)userContextCallback;

    if (componentName == NULL)
    {
        // The PnP protocol does not define a mechanism to report errors such as this to IoTHub, so 
        // the best we can do here is to log for diagnostics purposes.
        LogError("Property=%s arrived for TemperatureControl component itself.  This does not support writeable properties on it (all properties are on subcomponents)", propertyName);
    }
    else if (strcmp(componentName, g_thermostatComponent1Name) == 0)
    {
        PnP_ThermostatComponent_ProcessPropertyUpdate(g_thermostatHandle1, deviceClient, propertyName, propertyValue, version);
    }
    else if (strcmp(componentName, g_thermostatComponent2Name) == 0)
    {
        PnP_ThermostatComponent_ProcessPropertyUpdate(g_thermostatHandle2, deviceClient, propertyName, propertyValue, version);
    }
    else
    {
        LogError("Component=%s is not implemented by the TemperatureController", componentName);
    }
}

//
// PnP_TempControlComponent_DeviceTwinCallback is invoked by IoT SDK when a twin - either full twin or a PATCH update - arrives.
//
static void PnP_TempControlComponent_DeviceTwinCallback(DEVICE_TWIN_UPDATE_STATE updateState, const unsigned char* payload, size_t size, void* userContextCallback)
{
    // Invoke PnP_ProcessTwinData to actualy process the data.  PnP_ProcessTwinData uses a visitor pattern to parse
    // the JSON and then visit each property, invoking PnP_TempControlComponent_ApplicationPropertyCallback on each element.
    if (PnP_ProcessTwinData(updateState, payload, size, g_modeledComponents, g_numModeledComponents, PnP_TempControlComponent_ApplicationPropertyCallback, userContextCallback) == false)
    {
        // If we're unable to parse the JSON for any reason (typically because the JSON is malformed or we ran out of memory)
        // there is no action we can take beyond logging.
        LogError("Unable to process twin json.  Ignoring any desired property update requests");
    }
}

//
// PnP_TempControlComponent_SendWorkingSet sends a PnP telemetry indicating the current working set of the device, in 
// the unit of kibibytes (https://en.wikipedia.org/wiki/Kibibyte).
// This is a random value between g_workingSetMinimum and (g_workingSetMinimum+g_workingSetRandomModulo).
//
void PnP_TempControlComponent_SendWorkingSet(IOTHUB_DEVICE_CLIENT_LL_HANDLE deviceClient) 
{
    IOTHUB_MESSAGE_HANDLE messageHandle = NULL;
    IOTHUB_CLIENT_RESULT iothubResult;
    char workingSetTelemetryPayload[64];

    int workingSet = g_workingSetMinimum + (rand() % g_workingSetRandomModulo);

    if (snprintf(workingSetTelemetryPayload, sizeof(workingSetTelemetryPayload), g_workingSetTelemetryFormat, workingSet) < 0)
    {
        LogError("Unable to create a workingSet telemetry payload string");
    }
    else if ((messageHandle = PnP_CreateTelemetryMessageHandle(NULL, workingSetTelemetryPayload)) == NULL)
    {
        LogError("Unable to create telemetry message");
    }
    else if ((iothubResult = IoTHubDeviceClient_LL_SendEventAsync(deviceClient, messageHandle, NULL, NULL)) != IOTHUB_CLIENT_OK)
    {
        LogError("Unable to send telemetry message, error=%d", iothubResult);
    }

    IoTHubMessage_Destroy(messageHandle);
}

//
// PnP_TempControlComponent_ReportSerialNumber_Property sends the "serialNumber" property to IoTHub
//
static void PnP_TempControlComponent_ReportSerialNumber_Property(IOTHUB_DEVICE_CLIENT_LL_HANDLE deviceClient)
{
    IOTHUB_CLIENT_RESULT iothubClientResult;
    STRING_HANDLE jsonToSend = NULL;

    if ((jsonToSend = PnP_CreateReportedProperty(NULL, g_serialNumberPropertyName, g_serialNumberPropertyValue)) == NULL)
    {
        LogError("Unable to build serial number property");
    }
    else
    {
        const char* jsonToSendStr = STRING_c_str(jsonToSend);
        size_t jsonToSendStrLen = strlen(jsonToSendStr);

        if ((iothubClientResult = IoTHubDeviceClient_LL_SendReportedState(deviceClient, (const unsigned char*)jsonToSendStr, jsonToSendStrLen, NULL, NULL)) != IOTHUB_CLIENT_OK)
        {
            LogError("Unable to send reported state, error=%d", iothubClientResult);
        }
        else
        {
            LogInfo("Sending serialNumber property to IoTHub");
        }
    }

    STRING_delete(jsonToSend);
}

//
// GetConnectionSettingsFromConfiguration reads how to connect to the IoT Hub (using 
// either a connection string or a DPS symmetric key) from the configuration.
//
static bool GetConnectionSettingsFromConfiguration()
{
#ifdef USE_PROV_MODULE_FULL
    g_pnpDeviceConfiguration.securityType = PNP_CONNECTION_SECURITY_TYPE_DPS;
    g_pnpDeviceConfiguration.u.dpsConnectionAuth.endpoint = AZIOT_CONF_APP_PROVISION_ENDPOINT;
    g_pnpDeviceConfiguration.u.dpsConnectionAuth.idScope = AZIOT_CONF_APP_PROVISION_ID_SCOPE;
    g_pnpDeviceConfiguration.u.dpsConnectionAuth.deviceId = AZIOT_CONF_APP_PROVISION_REGISTRATION_ID;
    g_pnpDeviceConfiguration.u.dpsConnectionAuth.deviceKey = AZIOT_CONF_APP_PROVISION_SYMMETRIC_KEY;
#else
    g_pnpDeviceConfiguration.securityType = PNP_CONNECTION_SECURITY_TYPE_CONNECTION_STRING;
    g_pnpDeviceConfiguration.u.connectionString = AZIOT_CONF_APP_DEVICE_CONNECTION_STRING;
#endif

    return true;
}

//
// CreateDeviceClientAndAllocateComponents allocates the IOTHUB_DEVICE_CLIENT_LL_HANDLE the application will use along with thermostat components
// 
static IOTHUB_DEVICE_CLIENT_LL_HANDLE CreateDeviceClientAndAllocateComponents(void)
{
    IOTHUB_DEVICE_CLIENT_LL_HANDLE deviceClient = NULL;
    bool result;

    g_pnpDeviceConfiguration.deviceMethodCallback = PnP_TempControlComponent_DeviceMethodCallback;
    g_pnpDeviceConfiguration.deviceTwinCallback = PnP_TempControlComponent_DeviceTwinCallback;
    g_pnpDeviceConfiguration.enableTracing = g_hubClientTraceEnabled;
    g_pnpDeviceConfiguration.modelId = g_temperatureControllerModelId;

    if (GetConnectionSettingsFromConfiguration() == false)
    {
        LogError("Cannot read required environment variable(s)");
        result = false;
    }
    else if ((deviceClient = PnP_CreateDeviceClientLLHandle(&g_pnpDeviceConfiguration)) == NULL)
    {
        LogError("Failure creating IotHub device client");
        result = false;
    }
    else if ((g_thermostatHandle1 = PnP_ThermostatComponent_CreateHandle(g_thermostatComponent1Name)) == NULL)
    {
        LogError("Unable to create component handle for %s", g_thermostatComponent1Name);
        result = false;
    }
    else if ((g_thermostatHandle2 = PnP_ThermostatComponent_CreateHandle(g_thermostatComponent2Name)) == NULL)
    {
        LogError("Unable to create component handle for %s", g_thermostatComponent2Name);
        result = false;
    }
    else
    {
        result = true;
    }

    if (result == false)
    {
        PnP_ThermostatComponent_Destroy(g_thermostatHandle2);
        PnP_ThermostatComponent_Destroy(g_thermostatHandle1);
        if (deviceClient != NULL)
        {
            IoTHubDeviceClient_LL_Destroy(deviceClient);
            IoTHub_Deinit();
            deviceClient = NULL;
        }
    }

    return deviceClient;
}

// Global symbol referenced by the Azure SDK's port for Mbed OS, via "extern"
NetworkInterface *_defaultSystemNetwork;

int main(void)
{
    LogInfo("Connecting to the network");

    _defaultSystemNetwork = NetworkInterface::get_default_instance();
    if (_defaultSystemNetwork == nullptr) {
        LogError("No network interface found");
        return -1;
    }

    int ret = _defaultSystemNetwork->connect();
    if (ret != 0) {
        LogError("Connection error: %d", ret);
        return -1;
    }
    LogInfo("Connection success, MAC: %s", _defaultSystemNetwork->get_mac_address());

    LogInfo("Getting time from the NTP server");

    NTPClient ntp(_defaultSystemNetwork);
    ntp.set_server("time.google.com", 123);
    time_t timestamp = ntp.get_timestamp();
    if (timestamp < 0) {
        LogError("Failed to get the current time, error: %ld", (long)timestamp);
        return -1;
    }
    LogInfo("Time: %s", ctime(&timestamp));

    rtc_init();
    rtc_write(timestamp);
    time_t rtc_timestamp = rtc_read(); // verify it's been successfully updated
    LogInfo("RTC reports %s", ctime(&rtc_timestamp));

    IOTHUB_DEVICE_CLIENT_LL_HANDLE deviceClient = NULL;

    if ((deviceClient = CreateDeviceClientAndAllocateComponents()) == NULL)
    {
        LogError("Failure creating IotHub device client");
    }
    else
    {
        LogInfo("Successfully created device client.  Hit Control-C to exit program\n");

        int numberOfIterations = 0;

        // During startup, send the non-"writeable" properties.
        PnP_TempControlComponent_ReportSerialNumber_Property(deviceClient);
        PnP_DeviceInfoComponent_Report_All_Properties(g_deviceInfoComponentName, deviceClient);
        PnP_TempControlComponent_Report_MaxTempSinceLastReboot_Property(g_thermostatHandle1, deviceClient);
        PnP_TempControlComponent_Report_MaxTempSinceLastReboot_Property(g_thermostatHandle2, deviceClient);

        while (true)
        {
            // Wake up periodically to poll.  Even if we do not plan on sending telemetry, we still need to poll periodically in order to process
            // incoming requests from the server and to do connection keep alives.
            if ((numberOfIterations % g_sendTelemetryPollInterval) == 0)
            {
                PnP_TempControlComponent_SendWorkingSet(deviceClient);
                PnP_ThermostatComponent_SendTelemetry(g_thermostatHandle1, deviceClient);
                PnP_ThermostatComponent_SendTelemetry(g_thermostatHandle2, deviceClient);
            }

            IoTHubDeviceClient_LL_DoWork(deviceClient);
            ThreadAPI_Sleep(g_sleepBetweenPollsMs);
            numberOfIterations++;
        }

        // Free the memory allocated to track simulated thermostat.
        PnP_ThermostatComponent_Destroy(g_thermostatHandle2);
        PnP_ThermostatComponent_Destroy(g_thermostatHandle1);

        // Clean up the iothub sdk handle
        IoTHubDeviceClient_LL_Destroy(deviceClient);
        // Free all the sdk subsystem
        IoTHub_Deinit();
    }

    return 0;
}
