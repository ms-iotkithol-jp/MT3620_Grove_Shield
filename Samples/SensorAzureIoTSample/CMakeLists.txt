#  Copyright (c) Microsoft Corporation. All rights reserved.
#  Licensed under the MIT License.

cmake_minimum_required (VERSION 3.8)
project (grove-temp-humidity-barometer-bme280 C)

azsphere_configure_tools(TOOLS_REVISION "20.04")
azsphere_configure_api(TARGET_API_SET "5")


add_subdirectory("../../MT3620_Grove_Shield_Library" out)
# Create executable
add_executable (${PROJECT_NAME} main.c parson.c eventloop_timer_utilities.c)
target_include_directories(${PROJECT_NAME} PUBLIC ${AZURE_SPHERE_API_SET_DIR}/usr/include/azureiot)
target_compile_definitions(${PROJECT_NAME} PUBLIC AZURE_IOT_HUB_CONFIGURED)
target_link_libraries (${PROJECT_NAME} m azureiot applibs pthread gcc_s c MT3620_Grove_Shield_Library)

# Add MakeImage post-build command
include (${AZURE_SPHERE_MAKE_IMAGE_FILE})
