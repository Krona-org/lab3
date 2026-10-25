cmake_minimum_required(VERSION 3.16)
project(scanfile LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Папка назначения для скомпилированных библиотек
set(OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}")

add_library(scanfile SHARED
    scanfile.h
    scanfile.cpp
)

target_compile_definitions(scanfile PRIVATE SCANFILE_EXPORTS)
set_target_properties(scanfile PROPERTIES 
    WINDOWS_EXPORT_ALL_SYMBOLS TRUE
    ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
)

target_include_directories(scanfile PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)