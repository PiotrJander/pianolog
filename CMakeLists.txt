cmake_minimum_required(VERSION 3.16)

# Real Wi-Fi passwords and the update token live in sdkconfig.secrets (gitignored), applied
# on top of the committed defaults. Copy sdkconfig.secrets.example to start.
if(NOT DEFINED SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
endif()
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.secrets")
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.secrets")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(pianolog)
