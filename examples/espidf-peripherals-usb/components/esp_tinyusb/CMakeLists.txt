idf_build_get_property(target IDF_TARGET)

if(target STREQUAL "esp32s3")
    set(tusb_mcu "OPT_MCU_ESP32S3")
    set(tusb_family "esp32sx")
elseif(target STREQUAL "esp32s2")
    set(tusb_mcu "OPT_MCU_ESP32S2")
    set(tusb_family "esp32sx")
else()
    message(FATAL_ERROR "TinyUSB is not support on ${target}.")
    return()
endif()

set(compile_options
    "-DCFG_TUSB_MCU=${tusb_mcu}"
    "-DCFG_TUSB_DEBUG=${CONFIG_TINYUSB_DEBUG_LEVEL}"
    )

idf_component_get_property(freertos_component_dir freertos COMPONENT_DIR)

set(includes_private
    "hw/bsp/"
    "src/"
    "src/device"
    "additions/include_private"
    )

set(includes_public
    "src/"
    "additions/include"
    # The FreeRTOS API include convention in tinyusb is different from esp-idf
    "${freertos_component_dir}/FreeRTOS-Kernel/include/freertos"
    )

set(srcs
    "src/portable/espressif/${tusb_family}/dcd_${tusb_family}.c"
    "src/class/cdc/cdc_device.c"
    "src/class/hid/hid_device.c"
    "src/class/midi/midi_device.c"
    "src/class/msc/msc_device.c"
    "src/class/vendor/vendor_device.c"
    "src/common/tusb_fifo.c"
    "src/device/usbd_control.c"
    "src/device/usbd.c"
    "src/tusb.c"
    "additions/src/descriptors_control.c"
    "additions/src/tinyusb.c"
    "additions/src/usb_descriptors.c"
    )

if(NOT CONFIG_TINYUSB_NO_DEFAULT_TASK)
    list(APPEND srcs "additions/src/tusb_tasks.c")
endif()

if(CONFIG_TINYUSB_CDC_ENABLED)
    list(APPEND srcs
        "additions/src/cdc.c"
        "additions/src/tusb_cdc_acm.c"
        "additions/src/tusb_console.c"
        "additions/src/vfs_tinyusb.c"
        )
endif() # CONFIG_TINYUSB_CDC_ENABLED

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS ${includes_public}
                       PRIV_INCLUDE_DIRS ${includes_private}
                       PRIV_REQUIRES "vfs" "usb" "driver"
                       )

target_compile_options(${COMPONENT_LIB} PRIVATE ${compile_options})

# when no builtin class driver is enabled, an uint8_t data compared with `BUILTIN_DRIVER_COUNT` will always be false
set_source_files_properties("src/device/usbd.c" PROPERTIES COMPILE_FLAGS "-Wno-type-limits")
