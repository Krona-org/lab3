cmake_minimum_required(VERSION 3.16)
project(app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(app
    Application.h
    Application.cpp
    main.cpp
)

target_include_directories(app PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../menu"
    "${CMAKE_CURRENT_SOURCE_DIR}/../opendialogfile"
    "${CMAKE_CURRENT_SOURCE_DIR}/../"
)

target_link_libraries(app PRIVATE
    menu
    opendialogfile
    checkuserdata
    scanfile
)

add_custom_command(TARGET app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:checkuserdata>
        $<TARGET_FILE_DIR:app>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:scanfile>
        $<TARGET_FILE_DIR:app>
)