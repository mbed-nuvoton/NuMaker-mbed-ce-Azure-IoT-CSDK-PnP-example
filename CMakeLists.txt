# Copyright (c) 2022 ARM Limited. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.19)
cmake_policy(VERSION 3.19)

set(MBED_OS_PATH ${CMAKE_CURRENT_SOURCE_DIR}/mbed-os CACHE INTERNAL "")

# Set default path for mbed_app.json5 (no force-write for override)
set(MBED_APP_JSON_PATH mbed_app.json5 CACHE INTERNAL "")

# Set default path for custom_targets.json5 (no force-write for override)
set(CUSTOM_TARGETS_PATH custom_targets CACHE INTERNAL "")
set(CUSTOM_TARGETS_JSON_PATH ${CUSTOM_TARGETS_PATH}/custom_targets.json5 CACHE INTERNAL "")

# Load Mbed CE toolchain file and basic build system
include(${MBED_OS_PATH}/tools/cmake/app.cmake)

set(APP_PROJECT NUMAKER_MBED_CE_AZURE_IOT_CSDK_PNP_EXAMPLE)
set(APP_TARGET NuMaker-mbed-ce-Azure-IoT-CSDK-PnP-example)

# Set up project name
project(${APP_PROJECT})

# Add for custom targets
add_subdirectory(${CUSTOM_TARGETS_PATH})

# Add Mbed OS library
add_subdirectory(${MBED_OS_PATH})

# Add other libraries
add_subdirectory(mbed-ce-client-for-azure)
add_subdirectory(ntp-client)
if("COMPONENT_BMX055=1" IN_LIST MBED_TARGET_DEFINITIONS)
    add_subdirectory(drivers/sensor/COMPONENT_BMX055)
endif()

add_executable(${APP_TARGET})

target_include_directories(${APP_TARGET}
    PRIVATE
        .
        configs
        pnp/common
)

target_sources(${APP_TARGET}
    PRIVATE
        pnp/common/pnp_device_client_ll.c
        pnp/common/pnp_dps_ll.c
        pnp/common/pnp_protocol.c
)

# Use DPS
if("MBED_CONF_APP_USE_DPS=1" IN_LIST MBED_CONFIG_DEFINITIONS)
    target_sources(${APP_TARGET}
        PRIVATE
            hsm_custom/custom_hsm_example.c
    )

    target_compile_definitions(mbed-ce-client-for-azure
        PUBLIC
            USE_PROV_MODULE_FULL
            USE_PROV_MODULE
            HSM_AUTH_TYPE_CUSTOM
    )
endif()

if("NUMAKER_IOT_M487_DEV" IN_LIST MBED_TARGET_LABELS)
    target_sources(${APP_TARGET}
        PRIVATE
            pnp/pnp_numaker_iot_m487_dev/pnp_deviceinfo_component.cpp
            pnp/pnp_numaker_iot_m487_dev/pnp_motion_sensor_bmx055_component.cpp
            pnp/pnp_numaker_iot_m487_dev/pnp_numaker_iot_m487_dev.cpp
    )
else()
    target_sources(${APP_TARGET}
        PRIVATE
            pnp/pnp_temperature_controller/pnp_deviceinfo_component.c
            pnp/pnp_temperature_controller/pnp_temperature_controller.cpp
            pnp/pnp_temperature_controller/pnp_thermostat_component.c
    )
endif()

if("NUVOTON" IN_LIST MBED_TARGET_LABELS)
    target_sources(${APP_TARGET}
        PRIVATE
            "targets/TARGET_NUVOTON/platform_entropy.cpp"
    )
endif()

target_link_libraries(${APP_TARGET}
    PRIVATE
        mbed-os
        mbed-ce-client-for-azure
        ntp-client
        $<$<IN_LIST:COMPONENT_BMX055=1,${MBED_TARGET_DEFINITIONS}>:bmx055>
)

# Must call this for each target to set up bin file creation, code upload, etc
mbed_set_post_build(${APP_TARGET})

# Make sure this is the last line of the top-level build script
mbed_finalize_build()
