# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

add_compile_options(-w) # Turn off warnings until esp_tinyusb is updated IEC-86
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(espidf-arduino-blink)