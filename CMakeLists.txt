cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(reptile_admin)

idf_build_get_property(idf_version IDF_VERSION)
string(REGEX REPLACE "^v" "" _idf_version_sanitized "${idf_version}")
if(NOT _idf_version_sanitized STREQUAL "6.1.0")
    message(FATAL_ERROR
        "Ce projet nécessite ESP-IDF v6.1.0 release. IDF détecté: ${idf_version}. Corrigez IDF_PATH / export.sh avant de relancer CMake.")
endif()
