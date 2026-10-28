cmake_minimum_required(VERSION 3.22)

# Wi-Fi and broker credentials live in sdkconfig.defaults.local, which is not
# committed (see sdkconfig.defaults.local.example).
if(NOT DEFINED SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
endif()
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.local")
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.defaults.local")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(roomnode)
