# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Set the components to include the tests for.
set(EXTRA_COMPONENT_DIRS
        ../
        )

project(test_app_usb_device_esp_tinyusb)
