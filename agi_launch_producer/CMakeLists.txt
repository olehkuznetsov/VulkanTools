cmake_minimum_required(VERSION 3.22.1)
project(agi_launch_producer LANGUAGES CXX)

if(NOT ANDROID)
    return()
endif()

add_executable(agi_launch_producer main.cpp)

target_link_libraries(agi_launch_producer PRIVATE log)

install(TARGETS agi_launch_producer DESTINATION ${CMAKE_INSTALL_BINDIR})
