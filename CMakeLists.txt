cmake_minimum_required(VERSION 3.16)
project(gzip-ng VERSION 0.1.0 LANGUAGES C)

option(GZNG_ENABLE_TESTS "Build gzip-ng tests" ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# zlib-ng, found on the system or fetched and built with the native zng_ API.
find_package(zlib-ng CONFIG QUIET)
if(NOT TARGET zlib-ng::zlib)
    include(FetchContent)
    if(NOT DEFINED ZLIBNG_TAG)
        set(ZLIBNG_TAG 2.3.3)
    endif()
    set(ZLIB_COMPAT OFF CACHE BOOL "" FORCE)
    set(ZLIB_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(ZLIBNG_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(WITH_GTEST OFF CACHE BOOL "" FORCE)
    message(STATUS "Git checking out zlib-ng ${ZLIBNG_TAG}")
    FetchContent_Declare(zlib-ng
        GIT_REPOSITORY https://github.com/zlib-ng/zlib-ng.git
        GIT_TAG ${ZLIBNG_TAG})
    FetchContent_MakeAvailable(zlib-ng)
endif()

find_package(Threads REQUIRED)

add_library(gzng STATIC
    src/gzblock.c
    src/gzblockpool.c
    src/gzblockread.c
    src/gzblockwrite.c
    src/gzblock.h
    src/gzblock_p.h
    src/compress.c
    src/compress.h
    src/decompress.c
    src/decompress.h
    src/gzng.c
    src/process.c
    src/process.h
    src/options.c
    src/options.h
    src/gzng.h)
target_include_directories(gzng PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(gzng PRIVATE GZBLOCK_THREADS)
target_link_libraries(gzng PUBLIC zlib-ng::zlib Threads::Threads)

add_executable(gzip-ng src/main.c)
target_link_libraries(gzip-ng PRIVATE gzng)

if(GZNG_ENABLE_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
