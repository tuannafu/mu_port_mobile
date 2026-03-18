cmake_minimum_required(VERSION 3.22.1)
project(main52_android_stub LANGUAGES C CXX)

option(MAIN52_ANDROID_USE_SDL2 "Enable SDL2-backed Android shell" OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(main52_stub SHARED
    native-lib.cpp
)

target_compile_definitions(main52_stub PRIVATE
    MAIN52_ANDROID_STUB=1
)

find_library(log-lib log)
find_library(android-lib android)

target_link_libraries(main52_stub
    ${log-lib}
    ${android-lib}
)

if(MAIN52_ANDROID_USE_SDL2)
    message(STATUS "SDL2 hook requested. Wire vendor/imported SDL2 targets here before enabling real client code.")
endif()
