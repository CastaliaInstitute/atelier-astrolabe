set(priv_requires esp_timer)

if(${IDF_VERSION_MAJOR} LESS 6)
    list(APPEND priv_requires usb)       # USB PHY is part of usb component in IDF < 6.0
endif()

idf_component_register(SRCS usb_device_uac.c
                    INCLUDE_DIRS "include" "tusb" "tusb_uac"
                    PRIV_REQUIRES ${priv_requires})

include(package_manager)
cu_pkg_define_version(${CMAKE_CURRENT_LIST_DIR})
