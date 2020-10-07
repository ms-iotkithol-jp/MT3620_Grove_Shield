#  Copyright (c) Microsoft Corporation. All rights reserved.
#  Licensed under the MIT License.

cmake_minimum_required (VERSION 3.10)

project (SensorLiefDeviceAzureIoTSample C)

azsphere_configure_tools(TOOLS_REVISION "20.07")
azsphere_configure_api(TARGET_API_SET "6")

add_subdirectory("../../MT3620_Grove_Shield_Library" out)

# Create executable
add_executable (${PROJECT_NAME} main.c parson.c eventloop_timer_utilities.c liefdevice.c  )
target_include_directories(${PROJECT_NAME} PUBLIC ${AZURE_SPHERE_API_SET_DIR}/usr/include/azureiot)
target_compile_definitions(${PROJECT_NAME} PUBLIC AZURE_IOT_HUB_CONFIGURED)
target_link_libraries (${PROJECT_NAME} m azureiot applibs pthread gcc_s c MT3620_Grove_Shield_Library)
azsphere_target_hardware_definition(${PROJECT_NAME} TARGET_DIRECTORY "HardwareDefinitions/mt3620_rdb" TARGET_DEFINITION "template_appliance.json")

azsphere_target_add_image_package(${PROJECT_NAME})
