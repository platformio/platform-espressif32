set(priv_req "")

if(${IDF_VERSION_MAJOR} LESS 6)
    list(APPEND priv_req "usb")
endif()

idf_component_register(SRC_DIRS .
                       INCLUDE_DIRS .
                       PRIV_INCLUDE_DIRS "../../../include_private"
                       PRIV_REQUIRES ${priv_req}
                       REQUIRES unity
                       WHOLE_ARCHIVE)
