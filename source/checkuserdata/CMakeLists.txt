cmake_minimum_required(VERSION 3.16)
project(checkuserdata LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Папка назначения для скомпилированных библиотек
set(OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}")

add_library(checkuserdata SHARED
    checkuserdata.h
    checkuserdata.cpp
)

target_compile_definitions(checkuserdata PRIVATE CHECKUSERDATA_EXPORTS)
set_target_properties(checkuserdata PROPERTIES 
    WINDOWS_EXPORT_ALL_SYMBOLS TRUE
    ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_DIR}"
)

target_include_directories(checkuserdata PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)