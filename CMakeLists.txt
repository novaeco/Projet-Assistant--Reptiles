cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(reptile_admin)

idf_build_get_property(idf_version IDF_VERSION)
if(NOT DEFINED ENV{IDF_ALLOW_DEV_RELEASE_OVERRIDE})
    if(idf_version MATCHES "-dev" OR idf_version MATCHES "dirty")
        message(FATAL_ERROR "ESP-IDF v6.1.0 release required: detected ${idf_version}. Install the official v6.1.0 tag to build this project.")
    endif()
endif()
if(NOT idf_version STREQUAL "v6.1.0")
    message(WARNING "Project validated against ESP-IDF v6.1.0 release; detected ${idf_version}. Use v6.1.0 to avoid missing headers or unstable behavior.")
endif()
