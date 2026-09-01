cmake_minimum_required(VERSION 3.16)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

project(gzip-ng VERSION 2.0.0 LANGUAGES C)

include(CheckCCompilerFlag)
include(CheckCSourceCompiles)

option(GZNG_ENABLE_TESTS "Build gzip-ng tests" ON)
option(GZNG_ENABLE_BENCHMARKS "Build gzip-ng benchmarks" OFF)
option(GZNG_THREADS "Compress and decompress blocks on a thread pool" ON)
option(GZNG_SIMD "Scan for block boundaries with NEON or SSE2" ON)

set(GZNG_SANITIZER None CACHE STRING "Build with a sanitizer, None, Address, Memory, Thread, or Undefined")
set_property(CACHE GZNG_SANITIZER PROPERTY STRINGS "None" "Address" "Memory" "Thread" "Undefined")

# A sanitizer applies to everything built from here on, the fetched zlib-ng included.
if(DEFINED GZNG_SANITIZER)
    include(cmake/detect-sanitizer.cmake)

    if(GZNG_SANITIZER STREQUAL "Address")
        add_address_sanitizer()
    elseif(GZNG_SANITIZER STREQUAL "Memory")
        add_memory_sanitizer()
    elseif(GZNG_SANITIZER STREQUAL "Thread")
        add_thread_sanitizer()
    elseif(GZNG_SANITIZER STREQUAL "Undefined")
        add_undefined_sanitizer()
    endif()
endif()

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


# Static unless BUILD_SHARED_LIBS asks otherwise.
add_library(gzng
    codec.c
    reader.c
    writer.c
    gzblock.h
    buf.c
    buf.h
    codec.h
    compress.c
    compress.h
    cutter.c
    cutter.h
    decompress.c
    decompress.h
    format.c
    format.h
    gzng.c
    rolling.c
    rolling.h
    pipeline.h
    pool.c
    pool.h
    pool_p.h
    pool_pthread.c
    pool_win32.c
    scanner.c
    scanner.h
    util.h
    gzng.h)
target_include_directories(gzng PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(BUILD_SHARED_LIBS)
    # The exported API, a module definition on Windows and a version script for GNU ld.
    if(WIN32)
        target_sources(gzng PRIVATE win32/gzng.def)
    elseif(NOT APPLE)
        target_link_options(gzng PRIVATE "-Wl,--version-script,${CMAKE_CURRENT_SOURCE_DIR}/gzng.map")
        set_property(TARGET gzng APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/gzng.map)
    endif()
endif()
target_link_libraries(gzng PUBLIC zlib-ng::zlib)

if(GZNG_THREADS)
    find_package(Threads REQUIRED)
    target_compile_definitions(gzng PRIVATE GZBLOCK_THREADS)
    target_link_libraries(gzng PUBLIC Threads::Threads)
endif()
if(NOT GZNG_SIMD)
    target_compile_definitions(gzng PRIVATE GZBLOCK_NO_SIMD)
endif()

add_executable(gzip-ng main.c options.c options.h)
if(BUILD_SHARED_LIBS)
    # The format helpers are not part of the exported API, so the command builds its own copy.
    target_sources(gzip-ng PRIVATE format.c)
endif()
target_link_libraries(gzip-ng PRIVATE gzng)

if(GZNG_ENABLE_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

if(GZNG_ENABLE_BENCHMARKS)
    add_subdirectory(test/benchmarks)
endif()
