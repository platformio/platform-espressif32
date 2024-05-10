include($ENV{IDF_PATH}/tools/cmake/version.cmake)

idf_component_register(SRC_DIRS .
                       INCLUDE_DIRS .
                       REQUIRES unity esp_tinyusb
                       WHOLE_ARCHIVE)
