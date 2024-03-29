cmake_minimum_required(VERSION 3.22)
project(libusb_test
    LANGUAGES C
)

add_executable(libusb_test libusb_test.c)
target_link_libraries(libusb_test -lusb-1.0)

