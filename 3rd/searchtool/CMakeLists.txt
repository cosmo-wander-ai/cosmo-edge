cmake_minimum_required(VERSION 3.21)

project(AiboxSearchTool VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 QUIET COMPONENTS Widgets Network)
if(Qt6_FOUND)
    set(QT_PACKAGE Qt6)
else()
    find_package(Qt5 REQUIRED COMPONENTS Widgets Network)
    set(QT_PACKAGE Qt5)
endif()

add_executable(AiboxSearchTool WIN32
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
)

target_link_libraries(AiboxSearchTool PRIVATE
    ${QT_PACKAGE}::Widgets
    ${QT_PACKAGE}::Network
)

if(WIN32)
    target_link_libraries(AiboxSearchTool PRIVATE ws2_32)
endif()

if(MSVC)
    target_compile_options(AiboxSearchTool PRIVATE /W4 /permissive-)
else()
    target_compile_options(AiboxSearchTool PRIVATE -Wall -Wextra -Wpedantic)
endif()
