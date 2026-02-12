set(srcs
    "descriptors_control.c"
    "tinyusb.c"
    "usb_descriptors.c"
    "tinyusb_task.c"
    )

set(priv_req "")

if(${IDF_VERSION_MAJOR} LESS 6)
    list(APPEND priv_req "usb")
endif()

if(CONFIG_TINYUSB_CDC_ENABLED)
    list(APPEND srcs
        "cdc.c"
        "tinyusb_cdc_acm.c"
        )
    if(CONFIG_VFS_SUPPORT_IO)
        list(APPEND srcs
            "tinyusb_console.c"
            "vfs_tinyusb.c"
            )
    endif() # CONFIG_VFS_SUPPORT_IO
endif() # CONFIG_TINYUSB_CDC_ENABLED

if(CONFIG_TINYUSB_MSC_ENABLED)
    list(APPEND srcs
        "tinyusb_msc.c"
        "storage_spiflash.c"
        )
    if(CONFIG_SOC_SDMMC_HOST_SUPPORTED)
        list(APPEND srcs
            "storage_sdmmc.c"
            )
    endif() # CONFIG_SOC_SDMMC_HOST_SUPPORTED
endif() # CONFIG_TINYUSB_MSC_ENABLED


if(CONFIG_TINYUSB_NET_MODE_NCM)
    list(APPEND srcs
         "tinyusb_net.c"
         )
endif() # CONFIG_TINYUSB_NET_MODE_NCM

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "include_private"
                       PRIV_REQUIRES ${priv_req}
                       REQUIRES fatfs vfs
                       )

# Determine whether tinyusb is fetched from component registry or from local path
idf_build_get_property(build_components BUILD_COMPONENTS)
if(tinyusb IN_LIST build_components)
    set(tinyusb_name tinyusb) # Local component
else()
    set(tinyusb_name espressif__tinyusb) # Managed component
endif()

# Pass tusb_config.h from this component to TinyUSB
idf_component_get_property(tusb_lib ${tinyusb_name} COMPONENT_LIB)
target_include_directories(${tusb_lib} PRIVATE "include")
