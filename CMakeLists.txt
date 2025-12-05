cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{IDF_PATH} OR "$ENV{IDF_PATH}" STREQUAL "")
    message(FATAL_ERROR
        "ESP-IDF non détecté (IDF_PATH vide). Sous Windows, ouvrez \"ESP-IDF Command Prompt\" ou exécutez export.bat dans l'installation ESP-IDF 6.1.x avant de lancer idf.py.")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(reptile_admin)

# -----------------------------------------------------------------------------
# ESP-IDF version detection (robust against missing git metadata)
# Accept any ESP-IDF 6.1.x (including -dev/-dirty); warn if not an official
# release tag, error out for other major/minor branches.
# -----------------------------------------------------------------------------
set(_idf_version_string "")
set(_idf_major "")
set(_idf_minor "")
set(_idf_patch "")

# Prefer the canonical build property when available
idf_build_get_property(_idf_build_version IDF_VERSION)
if(_idf_build_version)
    set(_idf_version_string "${_idf_build_version}")
endif()

# Fallback to the legacy IDF_VERSION variable
if(NOT _idf_version_string AND DEFINED IDF_VERSION)
    set(_idf_version_string "${IDF_VERSION}")
endif()

# Use numeric components when provided by ESP-IDF
if(DEFINED IDF_VERSION_MAJOR)
    set(_idf_major "${IDF_VERSION_MAJOR}")
endif()
if(DEFINED IDF_VERSION_MINOR)
    set(_idf_minor "${IDF_VERSION_MINOR}")
endif()
if(DEFINED IDF_VERSION_PATCH)
    set(_idf_patch "${IDF_VERSION_PATCH}")
endif()

# Parse the version string if any field is still missing
if((NOT _idf_major OR NOT _idf_minor OR NOT _idf_patch) AND _idf_version_string)
    string(REGEX MATCH "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)" _idf_semver "${_idf_version_string}")
    if(_idf_semver)
        set(_idf_major "${CMAKE_MATCH_1}")
        set(_idf_minor "${CMAKE_MATCH_2}")
        set(_idf_patch "${CMAKE_MATCH_3}")
    endif()
endif()

if(NOT _idf_version_string)
    set(_idf_version_string "unknown")
endif()
set(DETECTED_IDF_VERSION "${_idf_major}.${_idf_minor}.${_idf_patch}")

if(NOT (_idf_major STREQUAL "6" AND _idf_minor STREQUAL "1"))
    message(FATAL_ERROR
        "Ce projet nécessite ESP-IDF 6.1.x. IDF détecté: ${DETECTED_IDF_VERSION} (${_idf_version_string}). Sélectionnez une toolchain ESP-IDF 6.1.x puis relancez idf.py.")
endif()

string(REGEX MATCH "^v?6\\.1\\.[0-9]+$" _idf_is_release "${_idf_version_string}")
if(NOT _idf_is_release)
    message(WARNING "ESP-IDF détecté: ${_idf_version_string} (composants=${DETECTED_IDF_VERSION}). Version non taguée release: la build continue mais vérifiez vos sources 6.1.x.")
endif()
