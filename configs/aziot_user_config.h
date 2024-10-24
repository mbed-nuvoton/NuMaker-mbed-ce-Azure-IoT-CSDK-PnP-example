/**
 * @brief Azure IoT user configuration
 *
 * @copyright Copyright (c) Microsoft Corporation.
 * Licensed under the MIT License.
 */

#ifndef __AZIOT_USER_CONFIG_H__
#define __AZIOT_USER_CONFIG_H__                1

/**
 * @brief Select support HSM type, HSM_TYPE_SYMM_KEY or HSM_TYPE_X509
 *        (DPS)
 */
#define AZIOT_CONF_APP_HSM_TYPE                     HSM_TYPE_SYMM_KEY

/**
 * @brief Registration ID when HSM_TYPE_SYMM_KEY is supported; Ignored
 *        for other HSM types (DPS)
 */
#define AZIOT_CONF_APP_PROVISION_REGISTRATION_ID    "<REGISTRATION_ID>"

/**
 * @brief Symmetric key when HSM_TYPE_SYMM_KEY is supported; Ignored for
 *        other HSM types (DPS)
 */
#define AZIOT_CONF_APP_PROVISION_SYMMETRIC_KEY      "<SYMMETRIC_KEY>"

/**
 * @brief Device provisioning service URI (DPS)
 */
#define AZIOT_CONF_APP_PROVISION_ENDPOINT           "global.azure-devices-provisioning.net"

/**
 * @brief Device provisioning service ID scope (DPS)
 */
#define AZIOT_CONF_APP_PROVISION_ID_SCOPE           "<ID_SCOPE>"

/**
 * @brief Device connection string for IoT Hub authentication (non-DPS)
 */
#define AZIOT_CONF_APP_DEVICE_CONNECTION_STRING     "<DEVICE_CONNECTION_STRING>"

#endif
