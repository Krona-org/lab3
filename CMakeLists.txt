cmake_minimum_required(VERSION 3.16)
project(lab3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Все скомпилированные библиотеки направляются в папку lib
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/lib")

add_subdirectory(source/menu)
add_subdirectory(source/opendialogfile)
add_subdirectory(source/checkuserdata)
add_subdirectory(source/scanfile)
add_subdirectory(source/app)