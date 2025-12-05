cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{IDF_PATH} OR "$ENV{IDF_PATH}" STREQUAL "")
    message(FATAL_ERROR
        "ESP-IDF non détecté (IDF_PATH vide). Sous Windows, ouvrez \"ESP-IDF Command Prompt\" ou exécutez export.bat dans l'installation ESP-IDF 6.1.0 avant de lancer idf.py.")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(reptile_admin)

set(DETECTED_IDF_VERSION "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}")
if(NOT (IDF_VERSION_MAJOR EQUAL 6 AND IDF_VERSION_MINOR EQUAL 1 AND IDF_VERSION_PATCH EQUAL 0))
    message(FATAL_ERROR
        "Ce projet nécessite ESP-IDF v6.1.0 release. IDF détecté: ${DETECTED_IDF_VERSION}. Sélectionnez l'ESP-IDF 6.1.0 (tag/release) puis relancez idf.py.")
endif()
