cmake_minimum_required(VERSION 3.16)
project(shellbeats)

if(NOT CMAKE_BUILD_TYPE)
    set(default_build_type "Debug")
    message(STATUS "Set the build type to `${default_build_type}` as none was specified.")
    set(CMAKE_BUILD_TYPE ${default_build_type} CACHE STRING "Chooce the build type." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()
message(STATUS "shellbeats Build Type: ${CMAKE_BUILD_TYPE}")

# Set the version for shellbeats
set(shellbeats_Version_Major 0)
set(shellbeats_Version_Minor 6)
set(shellbeats_Version_Patch 0)
set(shellbeats_Version_Status "dev")
set(PROJECT_VERSION
    "${shellbeats_Version_Major}.${shellbeats_Version_Minor}.${shellbeats_Version_Patch}${shellbeats_Version_Status}"
)
message(STATUS "${PROJECT_NAME} version: ${PROJECT_VERSION}")
# set(CMAKE_MINIMUM_REQUIRED_VERSION 3.2.1)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)
project(shellbeats VERSION 1.0.2 LANGUAGES C)

set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER gcc)

add_library(YTplaylist)
target_link_options(YTplaylist PRIVATE "-lncurses" "-pthread")
if(CMAKE_BUILD_TYPE MATCHES Debug)
    target_compile_options(YTplaylist PRIVATE "-Wall" "-Wextra"  "-pthread")
else()
    target_compile_options(YTplaylist PRIVATE "-Wall" "-Wextra" "-O2" "-pthread")
endif()
target_sources(YTplaylist
    PRIVATE
        youtube_playlist.c

    PUBLIC
        FILE_SET HEADERS
        FILES
        youtube_playlist.h
)
add_executable(shellbeats)
target_link_options(shellbeats PRIVATE "-lncurses" "-pthread")
if(CMAKE_BUILD_TYPE MATCHES Debug)
    target_compile_options(shellbeats PRIVATE "-Wall" "-Wextra"  "-pthread")
else()
    target_compile_options(shellbeats PRIVATE "-Wall" "-Wextra" "-O2" "-pthread")
endif()
target_sources(shellbeats
                    PRIVATE
                        shellbeats.c
)

target_link_libraries(shellbeats PRIVATE YTplaylist)
