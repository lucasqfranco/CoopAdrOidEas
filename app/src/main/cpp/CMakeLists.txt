cmake_minimum_required(VERSION 3.22.1)

project("CoopAndreasBridge" LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(
    CoopAndreasBridge
    SHARED
    coop_bridge.cpp
)

target_compile_options(
    CoopAndreasBridge
    PRIVATE
    -Wall
    -Wextra
    -O2
    -fvisibility=default
)

find_library(log-lib log)
find_library(android-lib android)

if(log-lib AND android-lib)
    target_link_libraries(
        CoopAndreasBridge
        ${log-lib}
        ${android-lib}
    )
endif()
